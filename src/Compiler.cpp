#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

#include "Compiler.hpp"
#include "Tree.hpp"
#include "TreePattern.hpp"

namespace Psi {
  namespace Compiler {
    FutureBase::FutureBase(CompileContext *context, const SourceLocation& location)
    : m_state(state_constructed), m_context(context), m_location(location) {
    }

    void FutureBase::call_void() {
      switch (m_state) {
      case state_constructed: run(); break;
      case state_ready: break;
      case state_running:
      case state_finished: throw_circular_exception(); break;
      default: PSI_FAIL("unknown future state");
      }
    }

    void FutureBase::dependency_call() {
      switch (m_state) {
      case state_constructed: run(); break;
      case state_ready:
      case state_finished: break;
      case state_running: throw_circular_exception(); break;
      default: PSI_FAIL("unknown future state");
      }
    }

    void FutureBase::run() {
      try {
        m_state = state_running;
        std::vector<boost::shared_ptr<FutureBase> > dependencies = run_callback();
        m_state = state_finished;

        for (std::vector<boost::shared_ptr<FutureBase> >::iterator ii = dependencies.begin(), ie = dependencies.end(); ii != ie; ++ii)
          (*ii)->dependency_call();

        m_state = state_ready;
      } catch (...) {
        m_state = state_failed;
        throw_failed_exception();
      }
    }

    void FutureBase::throw_circular_exception() {
      m_context->error_throw(m_location, "Circular dependency during code evaluation");
    }
    
    void FutureBase::throw_failed_exception() {
      throw CompileException();
    }

    class PhysicalSourceOriginFilename : public PhysicalSourceOrigin {
      std::string m_name;

    public:
      PhysicalSourceOriginFilename(const std::string& name) : m_name(name) {}

      virtual std::string name() {
        return m_name;
      }
    };

    boost::shared_ptr<PhysicalSourceOrigin> PhysicalSourceOrigin::filename(const std::string& name) {
      return boost::shared_ptr<PhysicalSourceOrigin>(new PhysicalSourceOriginFilename(name));
    }

    CompileException::CompileException() {
    }

    CompileException::~CompileException() throw() {
    }

    const char *CompileException::what() const throw() {
      return "Psi compile exception";
    }

    CompileContext::CompileContext(std::ostream *error_stream, std::ostream *warning_stream)
    : m_error_stream(error_stream), m_warning_stream(warning_stream), m_error_occurred(false) {
    }

    CompileContext::~CompileContext() {
    }

    void CompileContext::error(const SourceLocation& loc, const std::string& message) {
      *m_error_stream << boost::format("%s:%s: in '%s'\n") % loc.physical.origin->name() % loc.physical.first_line % loc.logical->full_name();
      *m_error_stream << boost::format("%s:%s:error:%s\n") % loc.physical.origin->name() % loc.physical.first_line % message;
      m_error_occurred = true;
    }

    void CompileContext::error_throw(const SourceLocation& loc, const std::string& message) {
      error(loc, message);
      throw CompileException();
    }

    void CompileContext::warning(const SourceLocation& loc, const std::string& message) {
      *m_warning_stream << boost::format("%s:%s: in '%s'\n") % loc.physical.origin->name() % loc.physical.first_line % loc.logical->full_name();
      *m_warning_stream << boost::format("%s:%s:warning:%s\n") % loc.physical.origin->name() % loc.physical.first_line % message;
    }

    LogicalSourceLocation::~LogicalSourceLocation() {
    }

    class RootLogicalSourceLocation : public LogicalSourceLocation {
    public:
      virtual boost::shared_ptr<LogicalSourceLocation> parent() const {
        return boost::shared_ptr<LogicalSourceLocation>();
      }

      virtual std::string full_name() const {
        return "";
      }
    };

    class NamedLogicalSourceLocation : public LogicalSourceLocation {
      std::string m_name;
      boost::shared_ptr<LogicalSourceLocation> m_parent;

    public:
      NamedLogicalSourceLocation(const std::string& name, const boost::shared_ptr<LogicalSourceLocation>& parent)
      : m_name(name), m_parent(parent) {
      }
      
      virtual boost::shared_ptr<LogicalSourceLocation> parent() const {
        return m_parent;
      }

      virtual std::string full_name() const {
        std::string parent_name = m_parent->full_name();
        if (!parent_name.empty()) {
          return parent_name + '.' + m_name;
        } else {
          return m_name;
        }
      }
    };

    boost::shared_ptr<LogicalSourceLocation> root_location() {
      return boost::shared_ptr<LogicalSourceLocation>(new RootLogicalSourceLocation());
    }

    boost::shared_ptr<LogicalSourceLocation> named_child_location(const boost::shared_ptr<LogicalSourceLocation>& parent, const std::string& name) {
        return boost::shared_ptr<LogicalSourceLocation>(new NamedLogicalSourceLocation(name, parent));
    }

    boost::shared_ptr<LogicalSourceLocation> anonymous_child_location(const boost::shared_ptr<LogicalSourceLocation>& parent) {
        return boost::shared_ptr<LogicalSourceLocation>(new NamedLogicalSourceLocation("(anonymous)", parent));
    }

    /**
     * \brief Compile an expression.
     *
     * \param expression Expression, usually as produced by the parser.
     * \param compile_context Compilation context.
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     * \param anonymize_location Whether to generate a new, anonymous location as a child of the current location.
     */
    DependentValue<TreePtr<> > compile_expression(const boost::shared_ptr<Parser::Expression>& expression,
                                                  CompileContext& compile_context,
                                                  const boost::shared_ptr<EvaluateContext>& evaluate_context,
                                                  const boost::shared_ptr<LogicalSourceLocation>& source,
                                                  bool anonymize_location) {

      SourceLocation location(expression->location, source);
      boost::shared_ptr<LogicalSourceLocation> first_source = anonymize_location ? anonymous_child_location(source) : source;

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        DependentValue<TreePtr<> > first = compile_expression(macro_expression.elements.front(), compile_context, evaluate_context, first_source, false);
        std::vector<boost::shared_ptr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());
        LookupResult<Macro::EvaluateCallback> first_lookup = first.value->type->macro->evaluate_lookup(rest);

        switch (first_lookup.type()) {
        case lookup_result_match: compile_context.error_throw(location, boost::format("Evaluate not supported by %s") % first.value->type->macro->name());
        case lookup_result_conflict: compile_context.error_throw(location, boost::format("Evaluate not supported by %s") % first.value->type->macro->name());
        default: break;
        }

        return first_lookup.value()(first, rest, compile_context, evaluate_context, location);
      }

      case Parser::expression_token: {
        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*expression);

        const char *bracket_operation_bracket = ":bracket";
        const char *bracket_operation_brace = ":brace";
        const char *bracket_operation_square_bracket = ":squareBracket";

        switch (token_expression.token_type) {
        case Parser::TokenExpression::bracket:
        case Parser::TokenExpression::brace:
        case Parser::TokenExpression::square_bracket: {
          const char *bracket_operation, *bracket_str;
          switch (token_expression.token_type) {
          case Parser::TokenExpression::bracket: bracket_operation = bracket_operation_bracket; bracket_str = "(...)"; break;
          case Parser::TokenExpression::brace: bracket_operation = bracket_operation_brace; bracket_str = "{...}"; break;
          case Parser::TokenExpression::square_bracket: bracket_operation = bracket_operation_square_bracket; bracket_str = "[...]"; break;
          default: PSI_FAIL("unreachable");
          }

          LookupResult<DependentValue<TreePtr<> > > first = evaluate_context->lookup(bracket_operation);
          switch (first.type()) {
          case lookup_result_none:
            compile_context.error_throw(location, boost::format("Context does not support evaluating %s brackets (%s operator missing)") % bracket_str % bracket_operation);
          case lookup_result_conflict:
            compile_context.error_throw(location, boost::format("Context does not support evaluating %s brackets (conflict getting %s)") % bracket_str % bracket_operation);
          default: break;
          }

          std::vector<boost::shared_ptr<Parser::Expression> > expression_list(1, expression);
          LookupResult<Macro::EvaluateCallback> first_lookup = first.value().value->type->macro->evaluate_lookup(expression_list);

          switch (first_lookup.type()) {
          case lookup_result_none:
            compile_context.error_throw(location, boost::format("Context does not support evaluating %s brackets (%s operator evaluation did not match)") % bracket_str % bracket_operation);
          case lookup_result_conflict:
            compile_context.error_throw(location, boost::format("Context does not support evaluating %s brackets (conflict on %s operator evaluation)") % bracket_str % bracket_operation);
          default: break;
          }

          return first_lookup.value()(first.value(), expression_list, compile_context, evaluate_context, location);
        }

        case Parser::TokenExpression::identifier: {
          std::string name(token_expression.text.begin, token_expression.text.end);
          LookupResult<DependentValue<TreePtr<> > > result = evaluate_context->lookup(name);

          switch (result.type()) {
          case lookup_result_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
          default: break;
          }

          return result.value();
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class EvaluateContextSequence : public EvaluateContext {
      std::vector<boost::shared_ptr<EvaluateContext> > m_children;

    public:
      EvaluateContextSequence(const boost::shared_ptr<EvaluateContext>& first, const boost::shared_ptr<EvaluateContext>& second) {
        m_children.push_back(first);
        m_children.push_back(second);
      }
      
      virtual LookupResult<DependentValue<TreePtr<> > > lookup(const std::string& name) {
        for (std::vector<boost::shared_ptr<EvaluateContext> >::const_iterator ii = m_children.begin(), ie = m_children.end(); ii != ie; ++ii) {
          LookupResult<DependentValue<TreePtr<> > > result = (*ii)->lookup(name);
          if (result.type() != lookup_result_none)
            return result;
        }

        return LookupResult<DependentValue<TreePtr<> > >::make_none();
      }
    };

    DependentValue<TreePtr<Block> > compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements,
                                                           CompileContext& compile_context,
                                                           const boost::shared_ptr<EvaluateContext>& evaluate_context,
                                                           const boost::shared_ptr<LogicalSourceLocation>& source) {
      boost::shared_ptr<EvaluateContextDictionary> local_evaluate_context(new EvaluateContextDictionary());
      boost::shared_ptr<EvaluateContext> child_evaluate_context(new EvaluateContextSequence(local_evaluate_context, evaluate_context));

      std::vector<std::pair<TreePtr<Statement>, boost::shared_ptr<Future<DependentValue<TreePtr<> > > > > > statement_trees;

      for (std::vector<boost::shared_ptr<Parser::NamedExpression> >::const_iterator ii = statements.begin(), ie = statements.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        if (named_expr.expression) {
          std::string expr_name;
          bool anonymize_location;
          boost::shared_ptr<LogicalSourceLocation> statement_location;
          if (named_expr.name) {
            expr_name.assign(named_expr.name->begin, named_expr.name->end);
            anonymize_location = true;
            statement_location = named_child_location(source, expr_name);
          } else {
            anonymize_location = false;
            statement_location = anonymous_child_location(source);
          }
          SourceLocation statement_location_full(named_expr.location, statement_location);

          boost::shared_ptr<Future<DependentValue<TreePtr<> > > > expression_future =
            Future<DependentValue<TreePtr<> > >::make(&compile_context, statement_location_full,
                                                      boost::bind(compile_expression, named_expr.expression, boost::ref(compile_context), child_evaluate_context, statement_location, anonymize_location));
          if (named_expr.name)
            local_evaluate_context->names.insert(std::make_pair(expr_name, expression_future));
          TreePtr<Statement> statement_tree = compile_context.new_tree<Statement>();
          statement_trees.push_back(std::make_pair(statement_tree, expression_future));
        }
      }

      TreePtr<Block> block = compile_context.new_tree<Block>();
      TreePtr<Statement> *next_statement_ptr = &block->statements;

      std::vector<boost::shared_ptr<FutureBase> > dependencies;
      for (std::vector<std::pair<TreePtr<Statement>, boost::shared_ptr<Future<DependentValue<TreePtr<> > > > > >::const_iterator ii = statement_trees.begin(), ie = statement_trees.end(); ii != ie; ++ii) {
        const DependentValue<TreePtr<> >& expr = ii->second->call();
        ii->first->value = expr.value;
        dependencies.insert(dependencies.end(), expr.dependencies.begin(), expr.dependencies.end());
        *next_statement_ptr = ii->first;
        next_statement_ptr = &ii->first->next;
      }

      return DependentValue<TreePtr<Block> >(block, dependencies);
    }
  }
}
