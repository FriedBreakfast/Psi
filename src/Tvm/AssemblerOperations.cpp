#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>

#include "Assembler.hpp"
#include "Arithmetic.hpp"
#include "ControlFlow.hpp"
#include "Derived.hpp"
#include "Number.hpp"
#include "Memory.hpp"

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      void check_n_values(const std::string& name, std::size_t expected, const Parser::CallExpression& expression) {
        if (expression.values.size() != expected)
          throw TvmUserError(str(boost::format("%s: %d value parameters expected") % name % expected));
      }

      void check_n_terms(const std::string& name, std::size_t expected, const Parser::CallExpression& expression) {
        if (expression.terms.size() != expected)
          throw TvmUserError(str(boost::format("%s: %d term parameters expected") % name % expected));
      }

      void default_parameter_setup(ArrayPtr<Term*> parameters, const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) {
        check_n_values(name, 0, expression);

	PSI_ASSERT(parameters.size() == expression.terms.size());
	std::size_t n = 0;
	for (UniqueList<Parser::Expression>::const_iterator it = expression.terms.begin(); it != expression.terms.end(); ++n, ++it)
	  parameters[n] = build_expression(context, *it);
      }

      template<typename T>
      struct DefaultFunctionalCallback {
        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          ScopedTermPtrArray<> parameters(expression.terms.size());
          default_parameter_setup(parameters.array(), name, context, expression);
          return context.context().get_functional(T(), parameters.array());
        }
      };

      template<typename T> struct type_name {static const char *s;};
      template<> const char *type_name<unsigned>::s = "unsigned integer";

      template<typename T>
      T token_lexical_cast(const std::string& name, unsigned index, const Parser::CallExpression& expression) {
        UniqueList<Parser::Token>::const_iterator it = expression.values.begin();
        std::advance(it, index);
        try {
          return boost::lexical_cast<T>(it->text);
        } catch (boost::bad_lexical_cast&) {
          throw AssemblerError(str(boost::format("%s: parameter %d should be a %s") % name % (index+1) % type_name<T>::s));
        }
      }

      struct IntTypeCallback {
        bool is_signed;

        IntTypeCallback(bool is_signed_) : is_signed(is_signed_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_values(name, 1, expression);
          check_n_terms(name, 0, expression);
          unsigned n_bits = token_lexical_cast<unsigned>(name, 0, expression);
          return context.context().get_functional_v(IntegerType(is_signed, n_bits));
        }
      };

      struct RealTypeCallback {
        RealType::Width width;

        RealTypeCallback(RealType::Width width_) : width(width_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_values(name, 0, expression);
          check_n_terms(name, 0, expression);
          return context.context().get_functional_v(RealType(width));
        }
      };

      struct BoolValueCallback {
        bool value;

        BoolValueCallback(bool value_) : value(value_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_values(name, 0, expression);
          check_n_terms(name, 0, expression);
          return context.context().get_functional_v(ConstantBoolean(value));
        }
      };

      struct IntValueCallback {
        bool is_signed;

        IntValueCallback(bool is_signed_) : is_signed(is_signed_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) {
          check_n_values(name, 2, expression);
          check_n_terms(name, 0, expression);
          unsigned n_bits = token_lexical_cast<unsigned>(name, 0, expression);
          mpz_class value;
          try {
            value.set_str(expression.values.back().text, 10);
          } catch (std::invalid_argument&) {
            throw AssemblerError(str(boost::format("%s: parameter 2 should be an integer") % name));
          }
          return context.context().get_functional_v(ConstantInteger(IntegerType(is_signed, n_bits), value));
        }
      };

      struct RealValueCallback {
        RealType::Width width;

        RealValueCallback(RealType::Width width_) : width(width_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression)  const {
          check_n_values(name, 1, expression);
          check_n_terms(name, 0, expression);
          mpf_class value;
          try {
            value.set_str(expression.values.front().text, 10);
          } catch (std::invalid_argument&) {
            throw AssemblerError(str(boost::format("%s: parameter 1 should be a number") % name));
          }
          return context.context().get_functional_v(ConstantReal(RealType(width), value));
        }
      };

      const std::tr1::unordered_map<std::string, FunctionalTermCallback> functional_ops =
        boost::assign::map_list_of<std::string, FunctionalTermCallback>
        ("type", DefaultFunctionalCallback<Metatype>())
        ("pointer", DefaultFunctionalCallback<PointerType>())
        ("bool", DefaultFunctionalCallback<BooleanType>())
        ("int", IntTypeCallback(true))
        ("uint", IntTypeCallback(false))
        ("float", RealTypeCallback(RealType::real_float))
        ("double", RealTypeCallback(RealType::real_double))
        ("true", BoolValueCallback(true))
        ("false", BoolValueCallback(false))
        ("c_int", IntValueCallback(true))
        ("c_uint", IntValueCallback(false))
        ("c_float", RealValueCallback(RealType::real_float))
        ("c_double", RealValueCallback(RealType::real_double))
        ("add", DefaultFunctionalCallback<IntegerAdd>())
        ("sub", DefaultFunctionalCallback<IntegerSubtract>())
        ("mul", DefaultFunctionalCallback<IntegerMultiply>())
        ("div", DefaultFunctionalCallback<IntegerDivide>())
        ("apply_phantom", DefaultFunctionalCallback<FunctionApplyPhantom>())
        ("array", DefaultFunctionalCallback<ArrayType>())
        ("c_array", DefaultFunctionalCallback<ArrayValue>())
        ("struct", DefaultFunctionalCallback<StructType>())
        ("c_struct", DefaultFunctionalCallback<StructValue>());

      template<typename T>
      struct DefaultInstructionCallback {
        InstructionTerm* operator () (const std::string& name, BlockTerm& block, AssemblerContext& context, const Parser::CallExpression& expression) const {
          ScopedTermPtrArray<> parameters(expression.terms.size());
          default_parameter_setup(parameters.array(), name, context, expression);
          return block.new_instruction(T(), parameters.array());
        }
      };

      const std::tr1::unordered_map<std::string, InstructionTermCallback> instruction_ops =
        boost::assign::map_list_of<std::string, InstructionTermCallback>
	("br", DefaultInstructionCallback<UnconditionalBranch>())
	("call", DefaultInstructionCallback<FunctionCall>())
	("cond_br", DefaultInstructionCallback<ConditionalBranch>())
	("return", DefaultInstructionCallback<Return>())
        ("alloca", DefaultInstructionCallback<Alloca>())
        ("load", DefaultInstructionCallback<Load>())
        ("store", DefaultInstructionCallback<Store>());
    }
  }
}
