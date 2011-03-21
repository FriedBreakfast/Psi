#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>

#include "Aggregate.hpp"
#include "Assembler.hpp"
#include "Instructions.hpp"
#include "Number.hpp"
#include "FunctionalBuilder.hpp"

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      void check_n_terms(const std::string& name, std::size_t expected, const Parser::CallExpression& expression) {
        if (expression.terms.size() != expected)
          throw TvmUserError(str(boost::format("%s: %d parameters expected") % name % expected));
      }

      void default_parameter_setup(ArrayPtr<Term*> parameters, AssemblerContext& context, const Parser::CallExpression& expression) {
	PSI_ASSERT(parameters.size() == expression.terms.size());
	std::size_t n = 0;
	for (UniqueList<Parser::Expression>::const_iterator it = expression.terms.begin(); it != expression.terms.end(); ++n, ++it)
	  parameters[n] = build_expression(context, *it);
      }
      
      struct NullaryOpCallback {
        typedef Term* (*GetterType) (Context&);
        GetterType getter;
        NullaryOpCallback(GetterType getter_) : getter(getter_) {}
        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) {
          check_n_terms(name, 0, expression);
          return getter(context.context());
        }
      };
      
      struct UnaryOpCallback {
        typedef Term* (*GetterType) (Term*);
        GetterType getter;
        UnaryOpCallback(GetterType getter_) : getter(getter_) {}
        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) {
          check_n_terms(name, 1, expression);
          return getter(build_expression(context, expression.terms.front()));
        }
      };

      struct BinaryOpCallback {
        typedef Term* (*GetterType) (Term*,Term*);
        GetterType getter;
        BinaryOpCallback(GetterType getter_) : getter(getter_) {}
        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) {
          check_n_terms(name, 2, expression);
          StaticArray<Term*, 2> parameters;
          default_parameter_setup(parameters, context, expression);
          return getter(parameters[0], parameters[1]);
        }
      };
      
      struct ContextArrayCallback {
        typedef Term* (*GetterType) (Context&,ArrayPtr<Term*const>);
        GetterType getter;
        ContextArrayCallback(GetterType getter_) : getter(getter_) {}
        Term* operator () (const std::string&, AssemblerContext& context, const Parser::CallExpression& expression) {
          ScopedArray<Term*> parameters(expression.terms.size());
          default_parameter_setup(parameters, context, expression);
          return getter(context.context(), parameters);
        }
      };
      
      struct TermPlusArrayCallback {
        typedef Term* (*GetterType) (Term*,ArrayPtr<Term*const>);
        GetterType getter;
        TermPlusArrayCallback(GetterType getter_) : getter(getter_) {}
        Term* operator () (const std::string&, AssemblerContext& context, const Parser::CallExpression& expression) {
          ScopedArray<Term*> parameters(expression.terms.size());
          default_parameter_setup(parameters, context, expression);
          return getter(parameters[0], parameters.slice(1, parameters.size()));
        }
      };
      
      struct StructElementCallback {
        typedef Term* (*GetterType) (Term*,unsigned);
        GetterType getter;
        StructElementCallback(GetterType getter_) : getter(getter_) {}
        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) {
          check_n_terms(name, 2, expression);
          
          Term *aggregate = build_expression(context, expression.terms.front());
          const Parser::Expression& index = expression.terms.back();
          
          if (index.expression_type != Parser::expression_literal)
            throw AssemblerError("Second parameter to struct_el is not an integer literal");

          const Parser::LiteralExpression& index_literal = checked_cast<const Parser::LiteralExpression&>(index);
          unsigned index_int = boost::lexical_cast<unsigned>(index_literal.value->text);
          
          return FunctionalBuilder::struct_element(aggregate, index_int);
        }
      };

      struct IntTypeCallback {
        IntegerType::Width width;
        bool is_signed;

        IntTypeCallback(IntegerType::Width width_, bool is_signed_) : width(width_), is_signed(is_signed_) {}

        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return FunctionalBuilder::int_type(context.context(), width, is_signed);
        }
      };

      struct FloatTypeCallback {
        FloatType::Width width;

        FloatTypeCallback(FloatType::Width width_) : width(width_) {}

        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
	  return FunctionalBuilder::float_type(context.context(), width);
        }
      };

      struct BoolValueCallback {
        bool value;

        BoolValueCallback(bool value_) : value(value_) {}

        Term* operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return FunctionalBuilder::bool_value(context.context(), value);
        }
      };

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
        ("bool", NullaryOpCallback(&FunctionalBuilder::bool_type))
        ("true", BoolValueCallback(true))
        ("false", BoolValueCallback(false))
        ("type", NullaryOpCallback(&FunctionalBuilder::type_type))
        ("empty", NullaryOpCallback(&FunctionalBuilder::empty_type))
        ("empty_v", NullaryOpCallback(&FunctionalBuilder::empty_value))
        ("byte", NullaryOpCallback(&FunctionalBuilder::byte_type))
        ("pointer", UnaryOpCallback(&FunctionalBuilder::pointer_type))
        ("add", BinaryOpCallback(&FunctionalBuilder::add))
        ("sub", BinaryOpCallback(&FunctionalBuilder::sub))
        ("mul", BinaryOpCallback(&FunctionalBuilder::mul))
        ("div", BinaryOpCallback(&FunctionalBuilder::div))
        ("array", BinaryOpCallback(&FunctionalBuilder::array_type))
        ("array_v", TermPlusArrayCallback(&FunctionalBuilder::array_value))
        ("array_el", BinaryOpCallback(&FunctionalBuilder::array_element))
        ("array_ep", BinaryOpCallback(&FunctionalBuilder::array_element_ptr))
        ("struct", ContextArrayCallback(&FunctionalBuilder::struct_type))
        ("struct_v", ContextArrayCallback(&FunctionalBuilder::struct_value))
        ("struct_el", StructElementCallback(&FunctionalBuilder::struct_element))
        ("struct_ep", StructElementCallback(&FunctionalBuilder::struct_element_ptr))
        ("union", ContextArrayCallback(&FunctionalBuilder::union_type))
        ("union_v", BinaryOpCallback(&FunctionalBuilder::union_value))
        ("union_el", BinaryOpCallback(&FunctionalBuilder::union_element))
        ("union_ep", BinaryOpCallback(&FunctionalBuilder::union_element_ptr))
        ("specialize", TermPlusArrayCallback(&FunctionalBuilder::specialize))
        ("pointer_cast", BinaryOpCallback(&FunctionalBuilder::pointer_cast))
        ("pointer_offset", BinaryOpCallback(&FunctionalBuilder::pointer_offset));

      template<typename T>
      struct DefaultInstructionCallback {
        InstructionTerm* operator () (const std::string&, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression) const {
          ScopedArray<Term*> parameters(expression.terms.size());
          default_parameter_setup(parameters, context, expression);
          return builder.insert_point().template create<T>(parameters);
        }
      };
      
      struct NullaryInstructionCallback {
        typedef InstructionTerm* (InstructionBuilder::*CreateType) ();
        CreateType create;
        NullaryInstructionCallback(CreateType create_) : create(create_) {}
        InstructionTerm* operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext&, const Parser::CallExpression& expression) const {
          check_n_terms(name, 0, expression);
          return (builder.*create)();
        }
      };

#define CALLBACK(ty) (ty::operation, DefaultInstructionCallback<ty>())

      const std::tr1::unordered_map<std::string, InstructionTermCallback> instruction_ops =
        boost::assign::map_list_of<std::string, InstructionTermCallback>
        CALLBACK(FunctionInvoke)
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
