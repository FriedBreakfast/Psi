#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>

#include "Assembler.hpp"
#include "Instructions.hpp"
#include "Operations.hpp"
#include "Type.hpp"

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      void check_n_terms(const std::string& name, std::size_t expected, const Parser::CallExpression& expression) {
        if (expression.terms.size() != expected)
          throw TvmUserError(str(boost::format("%s: %d term parameters expected") % name % expected));
      }

      void default_parameter_setup(ArrayPtr<Term*> parameters, AssemblerContext& context, const Parser::CallExpression& expression) {
	PSI_ASSERT(parameters.size() == expression.terms.size());
	std::size_t n = 0;
	for (UniqueList<Parser::Expression>::const_iterator it = expression.terms.begin(); it != expression.terms.end(); ++n, ++it)
	  parameters[n] = build_expression(context, *it);
      }

      template<typename T>
      struct DefaultFunctionalCallback {
        FunctionalTerm* operator () (const std::string&, AssemblerContext& context, const Parser::CallExpression& expression) const {
          ScopedTermPtrArray<> parameters(expression.terms.size());
          default_parameter_setup(parameters.array(), context, expression);
          return context.context().template get_functional<T>(typename T::Data(), parameters.array());
        }
      };

      struct IntTypeCallback {
        IntegerType::Width width;
        bool is_signed;

        IntTypeCallback(IntegerType::Width width_, bool is_signed_) : width(width_), is_signed(is_signed_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return IntegerType::get(context.context(), width, is_signed);
        }
      };

#if 0
      struct RealTypeCallback {
        RealType::Width width;

        RealTypeCallback(RealType::Width width_) : width(width_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return context.context().get_functional_v(RealType(width));
        }
      };
#endif

      struct BoolValueCallback {
        bool value;

        BoolValueCallback(bool value_) : value(value_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return ConstantBoolean::get(context.context(), value);
        }
      };

      const std::tr1::unordered_map<std::string, FunctionalTermCallback> functional_ops =
        boost::assign::map_list_of<std::string, FunctionalTermCallback>
        //("type", DefaultFunctionalCallback<Metatype>())
        //("pointer", DefaultFunctionalCallback<PointerType>())
        //("bool", DefaultFunctionalCallback<BooleanType>())
        ("byte", IntTypeCallback(IntegerType::i8, true))
        ("short", IntTypeCallback(IntegerType::i16, true))
        ("int", IntTypeCallback(IntegerType::i32, true))
        ("long", IntTypeCallback(IntegerType::i64, true))
        ("intptr", IntTypeCallback(IntegerType::iptr, true))
        ("ubyte", IntTypeCallback(IntegerType::i8, false))
        ("ushort", IntTypeCallback(IntegerType::i16, false))
        ("uint", IntTypeCallback(IntegerType::i32, false))
        ("ulong", IntTypeCallback(IntegerType::i64, false))
        ("uintptr", IntTypeCallback(IntegerType::iptr, false))
        //("float", RealTypeCallback(RealType::real_float))
        //("double", RealTypeCallback(RealType::real_double))
        ("true", BoolValueCallback(true))
        ("false", BoolValueCallback(false))
        ("add", DefaultFunctionalCallback<IntegerAdd>())
        ("sub", DefaultFunctionalCallback<IntegerSubtract>())
        ("mul", DefaultFunctionalCallback<IntegerMultiply>())
        ("div", DefaultFunctionalCallback<IntegerDivide>())
        ("specialize", DefaultFunctionalCallback<FunctionSpecialize>())
        ("array", DefaultFunctionalCallback<ArrayType>())
        ("array_c", DefaultFunctionalCallback<ArrayValue>())
        ("struct", DefaultFunctionalCallback<StructType>())
        ("struct_c", DefaultFunctionalCallback<StructValue>())
        ("union", DefaultFunctionalCallback<UnionType>())
        ("union_c", DefaultFunctionalCallback<UnionValue>());

      template<typename T>
      struct DefaultInstructionCallback {
        InstructionTerm* operator () (const std::string&, BlockTerm& block, AssemblerContext& context, const Parser::CallExpression& expression) const {
          ScopedTermPtrArray<> parameters(expression.terms.size());
          default_parameter_setup(parameters.array(), context, expression);
          return block.template new_instruction<T>(typename T::Data(), parameters.array());
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
