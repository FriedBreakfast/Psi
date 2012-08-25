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

      std::vector<ValuePtr<> > default_parameter_setup(AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
        std::vector<ValuePtr<> > parameters;
        for (UniqueList<Parser::Expression>::const_iterator it = expression.terms.begin(); it != expression.terms.end(); ++it)
          parameters.push_back(build_expression(context, *it, location));
        return parameters;
      }
      
      template<typename T>
      struct AsmCast {
        static ValuePtr<T> impl(const std::string& name, const ValuePtr<>& ptr) {
          ValuePtr<T> t = dyn_cast<T>(ptr);
          if (!t)
            throw TvmUserError(str(boost::format("%s: expected argument to be a %s") % name % T::operation));
          return t;
        }
      };
      
      template<>
      struct AsmCast<Value> {
        static ValuePtr<> impl(const std::string&, const ValuePtr<>& ptr) {
          return ptr;
        }
      };
      
      template<typename T>
      ValuePtr<T> asm_cast(const std::string& name, const ValuePtr<>& ptr) {
        return AsmCast<T>::impl(name, ptr);
      }
      
      struct NullaryOpCallback {
        typedef ValuePtr<> (*GetterType) (Context&,const SourceLocation&);
        GetterType getter;
        NullaryOpCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, 0, expression);
          return getter(context.context(), SourceLocation(expression.location, location));
        }
      };
      
      template<typename A=Value, typename R=Value>
      struct UnaryOpCallback {
        typedef ValuePtr<R> (*GetterType) (const ValuePtr<A>&,const SourceLocation&);
        GetterType getter;
        UnaryOpCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, 1, expression);
          ValuePtr<> arg = build_expression(context, expression.terms.front(), location);
          return getter(asm_cast<A>(name, arg), SourceLocation(expression.location, location));
        }
      };

      template<typename A, typename R>
      UnaryOpCallback<A,R> unary_op(ValuePtr<R> (*getter) (const ValuePtr<A>&,const SourceLocation&)) {
        return UnaryOpCallback<A,R>(getter);
      }

      template<typename A, typename B, typename R>
      struct BinaryOpCallback {
        typedef ValuePtr<R> (*GetterType) (const ValuePtr<A>&,const ValuePtr<B>&,const SourceLocation&);
        GetterType getter;
        BinaryOpCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, 2, expression);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return getter(asm_cast<A>(name, parameters[0]), asm_cast<B>(name, parameters[1]), SourceLocation(expression.location, location));
        }
      };
      
      template<typename A, typename B, typename R>
      BinaryOpCallback<A,B,R> binary_op(ValuePtr<R> (*getter) (const ValuePtr<A>&,const ValuePtr<B>&,const SourceLocation&)) {
        return BinaryOpCallback<A,B,R>(getter);
      }
      
      struct ContextArrayCallback {
        typedef ValuePtr<> (*GetterType) (Context&,const std::vector<ValuePtr<> >&,const SourceLocation&);
        GetterType getter;
        ContextArrayCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string&, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return getter(context.context(), parameters, SourceLocation(expression.location, location));
        }
      };
      
      struct TermPlusArrayCallback {
        typedef ValuePtr<> (*GetterType) (const ValuePtr<>&,const std::vector<ValuePtr<> >&,const SourceLocation&);
        GetterType getter;
        TermPlusArrayCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          if (parameters.empty())
            throw TvmUserError(str(boost::format("%s: at least one parameter expected") % name));
          ValuePtr<> first = parameters.front();
          parameters.erase(parameters.begin());
          return getter(first, parameters, SourceLocation(expression.location, location));
        }
      };
      
      struct StructElementCallback {
        typedef ValuePtr<> (*GetterType) (const ValuePtr<>&,unsigned,const SourceLocation&);
        GetterType getter;
        StructElementCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, 2, expression);
          
          ValuePtr<> aggregate = build_expression(context, expression.terms.front(), location);
          const Parser::Expression& index = expression.terms.back();
          
          if (index.expression_type != Parser::expression_literal)
            throw AssemblerError("Second parameter to struct_el is not an integer literal");

          const Parser::LiteralExpression& index_literal = checked_cast<const Parser::LiteralExpression&>(index);
          unsigned index_int = boost::lexical_cast<unsigned>(index_literal.value->text);
          
          return FunctionalBuilder::struct_element(aggregate, index_int, SourceLocation(expression.location, location));
        }
      };

      struct IntTypeCallback {
        IntegerType::Width width;
        bool is_signed;

        IntTypeCallback(IntegerType::Width width_, bool is_signed_) : width(width_), is_signed(is_signed_) {}

        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 0, expression);
          return FunctionalBuilder::int_type(context.context(), width, is_signed, SourceLocation(expression.location, location));
        }
      };

      struct FloatTypeCallback {
        FloatType::Width width;

        FloatTypeCallback(FloatType::Width width_) : width(width_) {}

        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 0, expression);
          return FunctionalBuilder::float_type(context.context(), width, SourceLocation(expression.location, location));
        }
      };

      struct BoolValueCallback {
        bool value;

        BoolValueCallback(bool value_) : value(value_) {}

        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 0, expression);
          return FunctionalBuilder::bool_value(context.context(), value, SourceLocation(expression.location, location));
        }
      };

      const boost::unordered_map<std::string, FunctionalTermCallback> functional_ops =
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
        ("pointer", unary_op(&FunctionalBuilder::pointer_type))
        ("add", binary_op(&FunctionalBuilder::add))
        ("sub", binary_op(&FunctionalBuilder::sub))
        ("mul", binary_op(&FunctionalBuilder::mul))
        ("div", binary_op(&FunctionalBuilder::div))
        ("neg", unary_op(&FunctionalBuilder::neg))
        ("undef", unary_op(&FunctionalBuilder::undef))
        ("zero", unary_op(&FunctionalBuilder::zero))
        ("array", binary_op(&FunctionalBuilder::array_type))
        ("array_v", TermPlusArrayCallback(&FunctionalBuilder::array_value))
        ("array_el", binary_op(&FunctionalBuilder::array_element))
        ("array_ep", binary_op(&FunctionalBuilder::array_element_ptr))
        ("struct", ContextArrayCallback(&FunctionalBuilder::struct_type))
        ("struct_v", ContextArrayCallback(&FunctionalBuilder::struct_value))
        ("struct_el", StructElementCallback(&FunctionalBuilder::struct_element))
        ("struct_ep", StructElementCallback(&FunctionalBuilder::struct_element_ptr))
        ("union", ContextArrayCallback(&FunctionalBuilder::union_type))
        ("union_v", binary_op(&FunctionalBuilder::union_value))
        ("union_el", binary_op(&FunctionalBuilder::union_element))
        ("union_ep", binary_op(&FunctionalBuilder::union_element_ptr))
        ("specialize", TermPlusArrayCallback(&FunctionalBuilder::specialize))
        ("pointer_cast", binary_op(&FunctionalBuilder::pointer_cast))
        ("pointer_offset", binary_op(&FunctionalBuilder::pointer_offset));

      struct NullaryInstructionCallback {
        typedef ValuePtr<Instruction> (InstructionBuilder::*CreateType) (const SourceLocation&);
        CreateType create;
        NullaryInstructionCallback(CreateType create_) : create(create_) {}
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext&, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 0, expression);
          return (builder.*create)(SourceLocation(expression.location, location));
        }
      };
      
      struct UnaryInstructionCallback {
        typedef ValuePtr<Instruction> (InstructionBuilder::*Callback) (const ValuePtr<>&, const SourceLocation&);
        Callback callback;
        UnaryInstructionCallback(Callback callback_) : callback(callback_) {}
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 1, expression);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return (builder.*callback)(parameters[0], SourceLocation(expression.location, location));
        }
      };

      struct BinaryInstructionCallback {
        typedef ValuePtr<Instruction> (InstructionBuilder::*Callback) (const ValuePtr<>&, const ValuePtr<>&, const SourceLocation&);
        Callback callback;
        BinaryInstructionCallback(Callback callback_) : callback(callback_) {}
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 2, expression);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return (builder.*callback)(parameters[0], parameters[1], SourceLocation(expression.location, location));
        }
      };

      struct CallCallback {
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          if (parameters.empty())
            throw TvmUserError(str(boost::format("%s: at least one parameter expected") % name));
          ValuePtr<> target = parameters.front();
          parameters.erase(parameters.begin());
          return builder.call(target, parameters, SourceLocation(expression.location, location));
        }
      };
      
      namespace {
        ValuePtr<Block> as_block(const std::string& name, const ValuePtr<>& ptr) {
          ValuePtr<Block> bl = dyn_cast<Block>(ptr);
          if (!bl)
            throw TvmUserError(str(boost::format("Parameter to %s is not a block") % name));
          return bl;
        }
      }
      
      struct UnconditionalBranchCallback {
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 1, expression);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return builder.br(as_block(name, parameters[0]), SourceLocation(expression.location, location));
        }
      };

      struct ConditionalBranchCallback {
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, 3, expression);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return builder.cond_br(parameters[0], as_block(name, parameters[1]), as_block(name, parameters[2]), SourceLocation(expression.location, location));
        }
      };
      
      struct AllocaCallback {
        ValuePtr<Instruction> operator () (const std::string&, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          SourceLocation loc(expression.location, location);
          switch (parameters.size()) {
          case 1: return builder.alloca_(parameters[0], loc);
          case 2: return builder.alloca_(parameters[0], parameters[1], loc);
          case 3: return builder.alloca_(parameters[0], parameters[1], parameters[2], loc);
          default: throw TvmUserError("alloca expects 1, 2 or 3 parameters");
          }
        }
      };

      const boost::unordered_map<std::string, InstructionTermCallback> instruction_ops =
        boost::assign::map_list_of<std::string, InstructionTermCallback>
        ("call", CallCallback())
        ("br", UnconditionalBranchCallback())
        ("cond_br", ConditionalBranchCallback())
        ("return", UnaryInstructionCallback(&InstructionBuilder::return_))
        ("alloca", AllocaCallback())
        ("load", UnaryInstructionCallback(&InstructionBuilder::load))
        ("store", BinaryInstructionCallback(&InstructionBuilder::store));
    }
  }
}
