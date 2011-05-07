#include "Compiler.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"

#include <boost/bind.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    boost::shared_ptr<Parser::TokenExpression> expression_as_token_type(const boost::shared_ptr<Parser::Expression>& expr, Parser::TokenExpression::TokenType type) {
      if (expr->expression_type != Parser::expression_token)
        return boost::shared_ptr<Parser::TokenExpression>();

      boost::shared_ptr<Parser::TokenExpression> cast_expr = checked_pointer_cast<Parser::TokenExpression>(expr);
      if (cast_expr->token_type != type)
        return boost::shared_ptr<Parser::TokenExpression>();

      return cast_expr;
    }

    class EvaluateContextOneName : public EvaluateContext {
      GCPtr<EvaluateContext> m_next;
      std::string m_name;
      TreePtr<> m_value;

      virtual void gc_visit(GCVisitor& visitor) {
        EvaluateContext::gc_visit(visitor);
        visitor % m_next % m_value;
      }

    public:
      EvaluateContextOneName(CompileContext& compile_context, const GCPtr<EvaluateContext>& next, const std::string& name, const TreePtr<>& value)
      : EvaluateContext(compile_context), m_next(next), m_name(name), m_value(value) {
      }

      virtual LookupResult<TreePtr<> > lookup(const std::string& name) {
        if (name == m_name) {
          return LookupResult<TreePtr<> >::make_match(m_value);
        } else if (m_next) {
          return m_next->lookup(name);
        } else {
          return LookupResult<TreePtr<> >::make_none();
        }
      }
    };

    class FunctionBodyCompiler : public Future {
      GCPtr<EvaluateContext> m_body_context;
      GCPtr<Function> m_function;
      boost::shared_ptr<Parser::TokenExpression> m_body;

      virtual void gc_visit(GCVisitor& visitor) {
        Future::gc_visit(visitor);
        visitor % m_body_context % m_function;
      }

      virtual void run() {
        std::vector<boost::shared_ptr<Parser::NamedExpression> > statements = Parser::parse_statement_list(m_body->text);
        TreePtr<> body_tree = compile_statement_list(statements, compile_context(), m_body_context, location());
        m_function->body = body_tree;

        body_tree->dependency->dependency_call();
        m_body_context.reset();
        m_function.reset();
      }

    public:
      FunctionBodyCompiler(CompileContext& compile_context,
                           const SourceLocation& location,
                           const GCPtr<EvaluateContext>& body_context,
                           const GCPtr<Function>& function,
                           const boost::shared_ptr<Parser::TokenExpression>& body)
      : Future(compile_context, location),
      m_body_context(body_context),
      m_function(function),
      m_body(body) {
      }
    };

    struct CompileFunctionCommonResult {
      TreePtr<FunctionType> type;
      std::map<std::string, unsigned> named_arguments;
    };

    CompileFunctionCommonResult compile_function_common(const PhysicalSourceLocation& arguments,
                                                        CompileContext& compile_context,
                                                        const GCPtr<EvaluateContext>& evaluate_context,
                                                        const SourceLocation& location) {
      Parser::ArgumentDeclarations parsed_arguments = Parser::parse_function_argument_declarations(arguments);

      CompileFunctionCommonResult result;
      result.type.reset(new FunctionType(compile_context));

      GCPtr<EvaluateContext> argument_context = evaluate_context;
      for (std::vector<boost::shared_ptr<Parser::NamedExpression> >::const_iterator ii = parsed_arguments.arguments.begin(), ib = parsed_arguments.arguments.begin(), ie = parsed_arguments.arguments.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        assert(named_expr.expression);

        std::string expr_name;
        bool anonymize_location;
        boost::shared_ptr<LogicalSourceLocation> argument_location;
        if (named_expr.name) {
          expr_name.assign(named_expr.name->begin, named_expr.name->end);
          anonymize_location = true;
          argument_location = named_child_location(location.logical, expr_name);
        } else {
          anonymize_location = false;
          argument_location = anonymous_child_location(location.logical);
        }

        TreePtr<> argument_type = compile_expression(named_expr.expression, compile_context, argument_context, argument_location, anonymize_location);
        TreePtr<Type> cast_argument_type = dynamic_pointer_cast<Type>(argument_type);
        if (!cast_argument_type)
          compile_context.error_throw(SourceLocation(named_expr.location, argument_location), "Function argument type expression does not evaluate to a type");
        
        TreePtr<FunctionTypeArgument> argument_value(new FunctionTypeArgument(compile_context));
        argument_value->type = cast_argument_type;
        result.type->arguments.push_back(argument_value);

        if (named_expr.name) {
          argument_context.reset(new EvaluateContextOneName(compile_context, argument_context, expr_name, argument_value));
          result.named_arguments[expr_name] = ii - ib;
        }
      }

      if (parsed_arguments.return_type) {
        TreePtr<> result_type = compile_expression(parsed_arguments.return_type, compile_context, argument_context, location.logical);
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
                                          const std::vector<boost::shared_ptr<Parser::Expression> >& arguments,
                                          CompileContext& compile_context,
                                          const GCPtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
      if (arguments.size() != 2)
        compile_context.error_throw(location, boost::format("function macro expects two arguments, got %s") % arguments.size());

      boost::shared_ptr<Parser::TokenExpression> parameters, body;

      if (!(parameters = expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "First (parameters) argument to definition is not a (...)");

      if (!(body = expression_as_token_type(arguments[1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Second (body) parameter to function definition is not a [...]");

      CompileFunctionCommonResult common = compile_function_common(parameters->text, compile_context, evaluate_context, location);

      TreePtr<Function> function(new Function(compile_context));
      function->type = common.type;
      function->arguments.reserve(common.type->arguments.size());

      std::map<TreePtr<>, TreePtr<> > argument_substitutions;
      for (std::vector<TreePtr<FunctionTypeArgument> >::iterator ii = common.type->arguments.begin(), ie = common.type->arguments.end(); ii != ie; ++ii) {
        TreePtr<FunctionArgument> arg(new FunctionArgument(compile_context));
        TreePtr<> type = (*ii)->type->rewrite(location, argument_substitutions);
        arg->type = dynamic_pointer_cast<Type>(type);
        if (!arg->type)
          compile_context.error_throw(location, "Rewritten function argument type is not a type");
        function->arguments.push_back(arg);
        argument_substitutions[*ii] = arg;
      }
      TreePtr<> result_type = common.type->result_type->rewrite(location, argument_substitutions);
      function->result_type = dynamic_pointer_cast<Type>(result_type);
      if (!function->result_type)
        compile_context.error_throw(location, "Rewritten function result type is not a type");

      std::map<std::string, TreePtr<> > argument_values;
      for (std::map<std::string, unsigned>::iterator ii = common.named_arguments.begin(), ie = common.named_arguments.end(); ii != ie; ++ii)
        argument_values[ii->first] = function->arguments[ii->second];

      GCPtr<EvaluateContext> body_context = evaluate_context_dictionary(compile_context, argument_values, evaluate_context);

      function->dependency.reset(new FunctionBodyCompiler(compile_context, location, body_context, function, body));

      return function;
    }

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
                                          const std::vector<boost::shared_ptr<Parser::Expression> >& arguments,
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
  }
}
