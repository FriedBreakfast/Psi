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
      GCPtr<Tree> m_value;

      virtual void gc_visit(GCVisitor& visitor) {
        EvaluateContext::gc_visit(visitor);
        visitor % m_next % m_value;
      }

    public:
      EvaluateContextOneName(CompileContext& compile_context, const GCPtr<EvaluateContext>& next, const std::string& name, const GCPtr<Tree>& value)
      : EvaluateContext(compile_context), m_next(next), m_name(name), m_value(value) {
      }

      virtual LookupResult<GCPtr<Tree> > lookup(const std::string& name) {
        if (name == m_name) {
          return LookupResult<GCPtr<Tree> >::make_match(m_value);
        } else if (m_next) {
          return m_next->lookup(name);
        } else {
          return LookupResult<GCPtr<Tree> >::make_none();
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
        GCPtr<Tree> body_tree = compile_statement_list(statements, context(), m_body_context, location());
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
      GCPtr<FunctionType> type;
      std::tr1::unordered_map<std::string, unsigned> named_arguments;
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

        GCPtr<Tree> argument_type = compile_expression(named_expr.expression, compile_context, argument_context, argument_location, anonymize_location);
        GCPtr<Type> cast_argument_type = dynamic_pointer_cast<Type>(argument_type);
        if (!cast_argument_type)
          compile_context.error_throw(SourceLocation(named_expr.location, argument_location), "Function argument type expression does not evaluate to a type");
        
        GCPtr<FunctionTypeArgument> argument_value(new FunctionTypeArgument(compile_context));
        argument_value->type = cast_argument_type;
        result.type->arguments.push_back(argument_value);

        if (named_expr.name) {
          argument_context.reset(new EvaluateContextOneName(compile_context, argument_context, expr_name, argument_value));
          result.named_arguments[expr_name] = ii - ib;
        }
      }

      return result;
    }
    
    GCPtr<Tree> compile_function_definition(const GCPtr<Tree>&,
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

      GCPtr<Function> function(new Function(compile_context));
      function->type = common.type;
      function->arguments.reserve(common.type->arguments.size());
      for (std::vector<GCPtr<FunctionTypeArgument> >::iterator ii = common.type->arguments.begin(), ie = common.type->arguments.end(); ii != ie; ++ii) {
        GCPtr<FunctionArgument> arg(new FunctionArgument(compile_context));
        PSI_FAIL("need to rewrite argument type");
        function->arguments.push_back(arg);
      }

      std::tr1::unordered_map<std::string, GCPtr<Tree> > argument_values;
      for (std::tr1::unordered_map<std::string, unsigned>::iterator ii = common.named_arguments.begin(), ie = common.named_arguments.end(); ii != ie; ++ii)
        argument_values[ii->first] = function->arguments[ii->second];

      GCPtr<EvaluateContext> body_context = evaluate_context_dictionary(compile_context, argument_values);

      function->dependency.reset(new FunctionBodyCompiler(compile_context, location, body_context, function, body));

      return function;
    }
  }
}
