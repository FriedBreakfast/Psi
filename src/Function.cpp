#include "Compiler.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"

#include <boost/bind.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    SharedPtr<Parser::TokenExpression> expression_as_token_type(const SharedPtr<Parser::Expression>& expr, Parser::TokenExpression::TokenType type) {
      if (expr->expression_type != Parser::expression_token)
        return SharedPtr<Parser::TokenExpression>();

      SharedPtr<Parser::TokenExpression> cast_expr = checked_pointer_cast<Parser::TokenExpression>(expr);
      if (cast_expr->token_type != type)
        return SharedPtr<Parser::TokenExpression>();

      return cast_expr;
    }

    class EvaluateContextOneName : public CompileImplementation {
      virtual void gc_visit(GCVisitor& visitor) {
        visitor % next;
      }

      struct Callback {
        LookupResult<TreePtr<> > lookup(const TreePtr<EvaluateContextOneName>& data, const String& name) {
          if (name == data->name) {
            return lookup_result_match(data->value);
          } else if (data->next) {
            return compile_implementation_wrap<EvaluateContextRef>(data->next).lookup(name);
          } else {
            return lookup_result_none;
          }
        }
      };

      static EvaluateContextWrapper<Callback, EvaluateContextOneName> m_vtable;

    public:
      String name;
      TreePtr<> value;
      TreePtr<CompileImplementation> next;

      EvaluateContextOneName(CompileContext& compile_context, const SourceLocation& location,
                             const String& name_, const TreePtr<>& value_, const TreePtr<CompileImplementation>& next_)
      : CompileImplementation(compile_context, location),
      name(name_), value(value_), next(next_) {
      }
    };

    EvaluateContextWrapper<EvaluateContextOneName::Callback, EvaluateContextOneName> EvaluateContextOneName::m_vtable;

    class FunctionBodyCompiler : public DependencyBase<FunctionBodyCompiler, Function> {
      TreePtr<CompileImplementation> m_body_context;
      SharedPtr<Parser::TokenExpression> m_body;

    public:
      FunctionBodyCompiler(const TreePtr<CompileImplementation>& body_context,
                           const SharedPtr<Parser::TokenExpression>& body)
      : m_body_context(body_context),
      m_body(body) {
      }

      void gc_visit(GCVisitor& visitor) {
        visitor % m_body_context;
      }

      void run(const TreePtr<Function>& function) {
        ArrayList<SharedPtr<Parser::NamedExpression> > statements = Parser::parse_statement_list(m_body->text);
        TreePtr<> body_tree = compile_statement_list(statements, m_body_context, function->location());
        function->body = body_tree;
        body_tree->dependency_complete();
      }
    };

    struct CompileFunctionCommonResult {
      TreePtr<FunctionType> type;
      std::map<String, unsigned> named_arguments;
    };

    CompileFunctionCommonResult compile_function_common(const PhysicalSourceLocation& arguments,
                                                        CompileContext& compile_context,
                                                        const TreePtr<CompileImplementation>& evaluate_context,
                                                        const SourceLocation& location) {
      Parser::ArgumentDeclarations parsed_arguments = Parser::parse_function_argument_declarations(arguments);

      CompileFunctionCommonResult result;
      result.type.reset(new FunctionType(compile_context, location));

      TreePtr<CompileImplementation> argument_context = evaluate_context;
      for (ArrayList<SharedPtr<Parser::NamedExpression> >::const_iterator ii = parsed_arguments.arguments.begin(), ib = parsed_arguments.arguments.begin(), ie = parsed_arguments.arguments.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        PSI_ASSERT(named_expr.expression);

        String expr_name = named_expr.name ? String(named_expr.name->begin, named_expr.name->end) : String();
        SourceLocation argument_location(named_expr.location, make_logical_location(location.logical, expr_name));

        TreePtr<> argument_type = compile_expression(named_expr.expression, argument_context, argument_location.logical);
        TreePtr<Type> cast_argument_type = dynamic_pointer_cast<Type>(argument_type);
        if (!cast_argument_type)
          compile_context.error_throw(argument_location, "Function argument type expression does not evaluate to a type");
        
        TreePtr<FunctionTypeArgument> argument_value(new FunctionTypeArgument(cast_argument_type, argument_location));
        result.type->arguments.push_back(argument_value);

        if (named_expr.name) {
          argument_context.reset(new EvaluateContextOneName(compile_context, argument_location, expr_name, argument_value, argument_context));
          result.named_arguments[expr_name] = ii - ib;
        }
      }

      if (parsed_arguments.return_type) {
        TreePtr<> result_type = compile_expression(parsed_arguments.return_type, argument_context, location.logical);
        TreePtr<Type> cast_result_type = dynamic_pointer_cast<Type>(result_type);
        if (!cast_result_type)
          compile_context.error_throw(location, "Function result type expression does not evaluate to a type");
        result.type->result_type = cast_result_type;
      } else {
        result.type->result_type = compile_context.empty_type();
      }

      return result;
    }

    TreePtr<> compile_function_definition(const TreePtr<>&,
                                          const std::vector<SharedPtr<Parser::Expression> >& arguments,
                                          CompileContext& compile_context,
                                          const TreePtr<CompileImplementation>& evaluate_context,
                                          const SourceLocation& location) {
      if (arguments.size() != 2)
        compile_context.error_throw(location, boost::format("function macro expects two arguments, got %s") % arguments.size());

      SharedPtr<Parser::TokenExpression> parameters, body;

      if (!(parameters = expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "First (parameters) argument to definition is not a (...)");

      if (!(body = expression_as_token_type(arguments[1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Second (body) parameter to function definition is not a [...]");

      CompileFunctionCommonResult common = compile_function_common(parameters->text, compile_context, evaluate_context, location);

      ArrayList<TreePtr<FunctionArgument> > argument_trees;
      std::map<TreePtr<>, TreePtr<> > argument_substitutions;

      for (ArrayList<TreePtr<FunctionTypeArgument> >::iterator ii = common.type->arguments.begin(), ie = common.type->arguments.end(); ii != ie; ++ii) {
        TreePtr<> arg_type = (*ii)->type()->rewrite(location, argument_substitutions);
        TreePtr<Type> cast_arg_type = dynamic_pointer_cast<Type>(arg_type);
        if (!cast_arg_type)
          compile_context.error_throw(location, "Rewritten function argument type is not a type");
        TreePtr<FunctionArgument> arg(new FunctionArgument(cast_arg_type, (*ii)->location()));
        argument_trees.push_back(arg);
        argument_substitutions[*ii] = arg;
      }
      TreePtr<> result_type = common.type->result_type->rewrite(location, argument_substitutions);
      TreePtr<Type> cast_result_type = dynamic_pointer_cast<Type>(result_type);
      if (!cast_result_type)
        compile_context.error_throw(location, "Rewritten function result type is not a type");

      std::map<String, TreePtr<> > argument_values;
      for (std::map<String, unsigned>::iterator ii = common.named_arguments.begin(), ie = common.named_arguments.end(); ii != ie; ++ii)
        argument_values[ii->first] = argument_trees[ii->second];

      TreePtr<CompileImplementation> body_context = evaluate_context_dictionary(compile_context, location, argument_values, evaluate_context);
      DependencyPtr body_compiler(new FunctionBodyCompiler(body_context, body));
      TreePtr<Function> function(new Function(common.type, location, move_ref(body_compiler)));
      function->result_type = cast_result_type;
      function->arguments.swap(argument_trees);

      return function;
    }

#if 0
    /**
     * \brief Callback to use for constructing interfaces which define functions.
     */
    class FunctionDefineCallback : public EvaluateCallback {
      virtual void gc_visit(GCVisitor& visitor) {
        EvaluateCallback::gc_visit(visitor);
      }

    public:
      FunctionDefineCallback(CompileContext& compile_context) : EvaluateCallback(compile_context) {
      }
      
      virtual TreePtr<> evaluate_callback(const TreePtr<>& value,
                                          const std::vector<SharedPtr<Parser::Expression> >& arguments,
                                          CompileContext& compile_context,
                                          const GCPtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
        return compile_function_definition(value, arguments, compile_context, evaluate_context, location);
      }
    };

    /**
     * \brief Create a callback to the function definition function.
     */
    TreePtr<> function_definition_object(CompileContext& compile_context) {
      GCPtr<EvaluateCallback> callback(new FunctionDefineCallback(compile_context));
      TreePtr<EmptyType> type(new EmptyType(compile_context));
      type->macro = make_interface(compile_context, "function", callback);
      return TreePtr<>(new EmptyValue(type));
    }
#endif
  }
}
