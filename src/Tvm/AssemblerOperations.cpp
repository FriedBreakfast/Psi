#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>

#include "Aggregate.hpp"
#include "Assembler.hpp"
#include "Instructions.hpp"
#include "Number.hpp"

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
          return context.context().template get_functional<T>(parameters.array());
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

      struct FloatTypeCallback {
        FloatType::Width width;

        FloatTypeCallback(FloatType::Width width_) : width(width_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
	  return FloatType::get(context.context(), width);
        }
      };

      struct BoolValueCallback {
        bool value;

        BoolValueCallback(bool value_) : value(value_) {}

        FunctionalTerm* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return BooleanValue::get(context.context(), value);
        }
      };

#define CALLBACK(ty) (ty::operation, DefaultFunctionalCallback<ty>())

      const std::tr1::unordered_map<std::string, FunctionalTermCallback> functional_ops =
        boost::assign::map_list_of<std::string, FunctionalTermCallback>
        ("i8", IntTypeCallback(IntegerType::i8, true))
        ("i16", IntTypeCallback(IntegerType::i16, true))
        ("i32", IntTypeCallback(IntegerType::i32, true))
        ("i64", IntTypeCallback(IntegerType::i64, true))
        ("i128", IntTypeCallback(IntegerType::i128, true))
        ("iptr", IntTypeCallback(IntegerType::iptr, true))
        ("ui8", IntTypeCallback(IntegerType::i8, false))
        ("ui16", IntTypeCallback(IntegerType::i16, false))
        ("ui32", IntTypeCallback(IntegerType::i32, false))
        ("ui64", IntTypeCallback(IntegerType::i64, false))
        ("ui128", IntTypeCallback(IntegerType::i128, false))
        ("uiptr", IntTypeCallback(IntegerType::iptr, false))
        ("fp32", FloatTypeCallback(FloatType::fp32))
        ("fp64", FloatTypeCallback(FloatType::fp64))
        ("fp128", FloatTypeCallback(FloatType::fp128))
        ("fp-x86-80", FloatTypeCallback(FloatType::fp_x86_80))
	("fp-ppc-128", FloatTypeCallback(FloatType::fp_ppc_128))
        ("true", BoolValueCallback(true))
        ("false", BoolValueCallback(false))
	CALLBACK(Metatype)
	CALLBACK(PointerType)
	CALLBACK(BooleanType)
	CALLBACK(IntegerAdd)
	CALLBACK(IntegerSubtract)
	CALLBACK(IntegerMultiply)
	CALLBACK(IntegerDivide)
	CALLBACK(FunctionSpecialize)
	CALLBACK(ArrayType)
	CALLBACK(ArrayValue)
	CALLBACK(StructType)
	CALLBACK(StructValue)
	CALLBACK(UnionType)
	CALLBACK(UnionValue);

#undef CALLBACK

      template<typename T>
      struct DefaultInstructionCallback {
        InstructionTerm* operator () (const std::string&, BlockTerm& block, AssemblerContext& context, const Parser::CallExpression& expression) const {
          ScopedTermPtrArray<> parameters(expression.terms.size());
          default_parameter_setup(parameters.array(), context, expression);
          return block.template new_instruction<T>(parameters.array());
        }
      };

#define CALLBACK(ty) (ty::operation, DefaultInstructionCallback<ty>())

      const std::tr1::unordered_map<std::string, InstructionTermCallback> instruction_ops =
        boost::assign::map_list_of<std::string, InstructionTermCallback>
	CALLBACK(FunctionCall)
	CALLBACK(UnconditionalBranch)
	CALLBACK(ConditionalBranch)
	CALLBACK(Return)
	CALLBACK(Alloca)
	CALLBACK(Load)
	CALLBACK(Store);

#undef CALLBACK
    }
  }
}
