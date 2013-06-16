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
      void check_n_terms(const std::string& name, AssemblerContext& context, std::size_t expected,
                         const Parser::CallExpression& expression, const LogicalSourceLocationPtr& logical_location) {
        if (expression.terms.size() != expected)
          context.error_context().error_throw(SourceLocation(expression.location, logical_location),
                                              boost::format("%s: %d parameters expected") % name % expected);
      }

      std::vector<ValuePtr<> > default_parameter_setup(AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
        std::vector<ValuePtr<> > parameters;
        for (UniqueList<Parser::Expression>::const_iterator it = expression.terms.begin(); it != expression.terms.end(); ++it)
          parameters.push_back(build_expression(context, *it, location));
        return parameters;
      }
      
      struct NullaryOpCallback {
        typedef ValuePtr<> (*GetterType) (Context&,const SourceLocation&);
        GetterType getter;
        NullaryOpCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, context, 0, expression, location);
          return getter(context.context(), SourceLocation(expression.location, location));
        }
      };
      
      struct UnaryOpCallback {
        typedef ValuePtr<> (*GetterType) (const ValuePtr<>&,const SourceLocation&);
        GetterType getter;
        UnaryOpCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, context, 1, expression, location);
          return getter(build_expression(context, expression.terms.front(), location), SourceLocation(expression.location, location));
        }
      };

      struct BinaryOpCallback {
        typedef ValuePtr<> (*GetterType) (const ValuePtr<>&,const ValuePtr<>&,const SourceLocation&);
        GetterType getter;
        BinaryOpCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, context, 2, expression, location);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return getter(parameters[0], parameters[1], SourceLocation(expression.location, location));
        }
      };
      
      struct UnaryOrBinaryCallback {
        typedef UnaryOpCallback::GetterType UnaryGetterType;
        typedef BinaryOpCallback::GetterType BinaryGetterType;
        
        UnaryGetterType unary_getter;
        BinaryGetterType binary_getter;
        
        UnaryOrBinaryCallback(UnaryGetterType unary_getter_, BinaryGetterType binary_getter_) : unary_getter(unary_getter_), binary_getter(binary_getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          if ((expression.terms.size() != 1) && (expression.terms.size() != 2))
            context.error_context().error_throw(SourceLocation(expression.location, location),
                                                boost::format("%s: 1 or 2 parameters expected") % name);

          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          if (parameters.size() == 1)
            return unary_getter(parameters[0], SourceLocation(expression.location, location));
          else
            return binary_getter(parameters[0], parameters[1], SourceLocation(expression.location, location));
        }
      };
      
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
            context.error_context().error_throw(SourceLocation(expression.location, location),
                                                boost::format("%s: at least one parameter expected") % name);
          ValuePtr<> first = parameters.front();
          parameters.erase(parameters.begin());
          return getter(first, parameters, SourceLocation(expression.location, location));
        }
      };
      
      struct TermPlusIndexCallback {
        typedef ValuePtr<> (*GetterType) (const ValuePtr<>&,unsigned,const SourceLocation&);
        GetterType getter;
        TermPlusIndexCallback(GetterType getter_) : getter(getter_) {}
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          check_n_terms(name, context, 2, expression, location);
          
          ValuePtr<> aggregate = build_expression(context, expression.terms.front(), location);
          const Parser::Expression& index = expression.terms.back();
          
          if (index.expression_type != Parser::expression_literal)
            throw AssemblerError("Second parameter to struct_el is not an integer literal");

          const Parser::LiteralExpression& index_literal = checked_cast<const Parser::LiteralExpression&>(index);
          unsigned index_int = boost::lexical_cast<unsigned>(index_literal.value->text);
          
          return getter(aggregate, index_int, SourceLocation(expression.location, location));
        }
      };
      
      struct UprefCallback {
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          if (expression.terms.empty())
            throw AssemblerError(name + " requires at least one argument");
          
          UniqueList<Parser::Expression>::const_iterator ii = expression.terms.begin(), ie = expression.terms.end();
          
          ValuePtr<> upref = build_expression(context, *ii, location);
          ValuePtr<> type;
          if (!isa<UpwardReferenceType>(upref->type())) {
            type = upref;
            upref.reset();
          }

          SourceLocation source_location(expression.location, location);
          ++ii;
          for (; ii != ie; ++ii) {
            ValuePtr<> cur = build_expression(context, *ii, location);
            if (cur->is_type()) {
              if (type)
                throw AssemblerError("types cannot appear next to each other in " + name + " operation");
              type = cur;
            } else {
              upref = FunctionalBuilder::upref(type, cur, upref, source_location);
              type.reset();
            }
          }
          
          return upref;
        }
      };

      struct IntTypeCallback {
        IntegerType::Width width;
        bool is_signed;

        IntTypeCallback(IntegerType::Width width_, bool is_signed_) : width(width_), is_signed(is_signed_) {}

        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 0, expression, location);
          return FunctionalBuilder::int_type(context.context(), width, is_signed, SourceLocation(expression.location, location));
        }
      };

      struct FloatTypeCallback {
        FloatType::Width width;

        FloatTypeCallback(FloatType::Width width_) : width(width_) {}

        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 0, expression, location);
          return FunctionalBuilder::float_type(context.context(), width, SourceLocation(expression.location, location));
        }
      };

      struct BoolValueCallback {
        bool value;

        BoolValueCallback(bool value_) : value(value_) {}

        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 0, expression, location);
          return FunctionalBuilder::bool_value(context.context(), value, SourceLocation(expression.location, location));
        }
      };
      
      struct FoldRightCallback {
        typedef ValuePtr<> (*GetterType) (const ValuePtr<>&,const ValuePtr<>&,const SourceLocation&);
        GetterType getter;
        FoldRightCallback(GetterType getter_) : getter(getter_) {}
        
        ValuePtr<> operator () (const std::string& name, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) {
          UniqueList<Parser::Expression>::const_iterator ii = expression.terms.begin(), ie = expression.terms.end();
          if (ii == ie)
            context.error_context().error_throw(SourceLocation(expression.location, location),
                                                boost::format("%s operation requires at least one argument") % name);
          
          SourceLocation source_location(expression.location, location);
          ValuePtr<> value = build_expression(context, *ii, location);
          ++ii;
          for (; ii != ie; ++ii)
            value = getter(value, build_expression(context, *ii, location), source_location);
          return value;
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
        ("constant", UnaryOpCallback(&FunctionalBuilder::const_type))
        ("empty", NullaryOpCallback(&FunctionalBuilder::empty_type))
        ("empty_v", NullaryOpCallback(&FunctionalBuilder::empty_value))
        ("byte", NullaryOpCallback(&FunctionalBuilder::byte_type))
        ("pointer", UnaryOrBinaryCallback(&FunctionalBuilder::pointer_type, &FunctionalBuilder::pointer_type))
        ("upref_type", NullaryOpCallback(&FunctionalBuilder::upref_type))
        ("upref", UprefCallback())
        ("upref_null", NullaryOpCallback(&FunctionalBuilder::upref_null))
        ("outer_ptr", UnaryOpCallback(&FunctionalBuilder::outer_ptr))
        ("add", BinaryOpCallback(&FunctionalBuilder::add))
        ("sub", BinaryOpCallback(&FunctionalBuilder::sub))
        ("mul", BinaryOpCallback(&FunctionalBuilder::mul))
        ("div", BinaryOpCallback(&FunctionalBuilder::div))
        ("neg", UnaryOpCallback(&FunctionalBuilder::neg))
        ("cmp_eq", BinaryOpCallback(&FunctionalBuilder::cmp_eq))
        ("cmp_ne", BinaryOpCallback(&FunctionalBuilder::cmp_ne))
        ("cmp_gt", BinaryOpCallback(&FunctionalBuilder::cmp_gt))
        ("cmp_ge", BinaryOpCallback(&FunctionalBuilder::cmp_ge))
        ("cmp_lt", BinaryOpCallback(&FunctionalBuilder::cmp_lt))
        ("cmp_le", BinaryOpCallback(&FunctionalBuilder::cmp_le))
        ("bitcast", BinaryOpCallback(&FunctionalBuilder::bit_cast))
        ("shl", BinaryOpCallback(&FunctionalBuilder::bit_shl))
        ("shr", BinaryOpCallback(&FunctionalBuilder::bit_shr))
        ("undef", UnaryOpCallback(&FunctionalBuilder::undef))
        ("zero", UnaryOpCallback(&FunctionalBuilder::zero))
        ("array", BinaryOpCallback(&FunctionalBuilder::array_type))
        ("array_v", TermPlusArrayCallback(&FunctionalBuilder::array_value))
        ("struct", ContextArrayCallback(&FunctionalBuilder::struct_type))
        ("struct_v", ContextArrayCallback(&FunctionalBuilder::struct_value))
        ("union", ContextArrayCallback(&FunctionalBuilder::union_type))
        ("union_v", BinaryOpCallback(&FunctionalBuilder::union_value))
        ("apply", TermPlusArrayCallback(&FunctionalBuilder::apply_type))
        ("apply_v", BinaryOpCallback(&FunctionalBuilder::apply_value))
        ("element", FoldRightCallback(&FunctionalBuilder::element_value))
        ("gep", FoldRightCallback(&FunctionalBuilder::element_ptr))
        ("specialize", TermPlusArrayCallback(&FunctionalBuilder::specialize))
        ("introduce_exists", BinaryOpCallback(&FunctionalBuilder::introduce_exists))
        ("pointer_cast", BinaryOpCallback(&FunctionalBuilder::pointer_cast))
        ("pointer_offset", BinaryOpCallback(&FunctionalBuilder::pointer_offset))
        ("unwrap", UnaryOpCallback(&FunctionalBuilder::unwrap))
        ("unwrap_param", TermPlusIndexCallback(&FunctionalBuilder::unwrap_param));

      struct NullaryInstructionCallback {
        typedef ValuePtr<Instruction> (InstructionBuilder::*CreateType) (const SourceLocation&);
        CreateType create;
        NullaryInstructionCallback(CreateType create_) : create(create_) {}
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 0, expression, location);
          return (builder.*create)(SourceLocation(expression.location, location));
        }
      };
      
      struct UnaryInstructionCallback {
        typedef ValuePtr<Instruction> (InstructionBuilder::*Callback) (const ValuePtr<>&, const SourceLocation&);
        Callback callback;
        UnaryInstructionCallback(Callback callback_) : callback(callback_) {}
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 1, expression, location);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return (builder.*callback)(parameters[0], SourceLocation(expression.location, location));
        }
      };

      struct BinaryInstructionCallback {
        typedef ValuePtr<Instruction> (InstructionBuilder::*Callback) (const ValuePtr<>&, const ValuePtr<>&, const SourceLocation&);
        Callback callback;
        BinaryInstructionCallback(Callback callback_) : callback(callback_) {}
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 2, expression, location);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          return (builder.*callback)(parameters[0], parameters[1], SourceLocation(expression.location, location));
        }
      };

      struct CallCallback {
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          if (parameters.empty())
            context.error_context().error_throw(SourceLocation(expression.location, location),
                                                boost::format("%s: at least one parameter expected") % name);
          ValuePtr<> target = parameters.front();
          parameters.erase(parameters.begin());
          return builder.call(target, parameters, SourceLocation(expression.location, location));
        }
      };
      
      namespace {
        ValuePtr<Block> as_block(const std::string& name, AssemblerContext& context, const ValuePtr<>& ptr, const SourceLocation& location) {
          ValuePtr<Block> bl = dyn_cast<Block>(ptr);
          if (!bl)
            context.error_context().error_throw(location, boost::format("Parameter to %s is not a block") % name);
          return bl;
        }
      }
      
      struct UnconditionalBranchCallback {
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 1, expression, location);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          SourceLocation result_location(expression.location, location);
          return builder.br(as_block(name, context, parameters[0], result_location), result_location);
        }
      };

      struct ConditionalBranchCallback {
        ValuePtr<Instruction> operator () (const std::string& name, InstructionBuilder& builder, AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& location) const {
          check_n_terms(name, context, 3, expression, location);
          std::vector<ValuePtr<> > parameters = default_parameter_setup(context, expression, location);
          SourceLocation result_location(expression.location, location);
          return builder.cond_br(parameters[0],
                                 as_block(name, context, parameters[1], result_location),
                                 as_block(name, context, parameters[2], result_location),
                                 result_location);
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
          default: context.error_context().error_throw(SourceLocation(expression.location, location), "alloca expects 1, 2 or 3 parameters");
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
        ("alloca_const", UnaryInstructionCallback(&InstructionBuilder::alloca_const))
        ("freea", UnaryInstructionCallback(&InstructionBuilder::freea))
        ("eval", UnaryInstructionCallback(&InstructionBuilder::eval))
        ("load", UnaryInstructionCallback(&InstructionBuilder::load))
        ("store", BinaryInstructionCallback(&InstructionBuilder::store))
        ("solidify", UnaryInstructionCallback(&InstructionBuilder::solidify));
    }
  }
}
