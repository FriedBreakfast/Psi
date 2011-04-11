#include <boost/format.hpp>
#include <boost/next_prior.hpp>

#include "Compiler.hpp"
#include "Tree.hpp"
#include "TreePattern.hpp"

namespace Psi {
  namespace Compiler {
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
      throw CompileException("Circular dependency found");
    }
    
    void FutureBase::throw_failed_exception() {
      throw CompileException("Future failed");
    }

    CompileException::CompileException(const std::string& message)
    : runtime_error(message) {
    }

    CompileException::~CompileException() throw() {
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

    DependentValue<TreePtr<> > compile_expression(const boost::shared_ptr<Parser::Expression>& expression,
                                                  EvaluateContext& context,
                                                  const boost::shared_ptr<LogicalSourceLocation>& source,
                                                  bool anonymize_location) {

      SourceLocation location(expression->location, source);
      boost::shared_ptr<LogicalSourceLocation> first_source = anonymize_location ? anonymous_child_location(source) : source;

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        DependentValue<TreePtr<> > first = compile_expression(macro_expression.elements.front(), context, first_source, false);
        std::vector<boost::shared_ptr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());
        LookupResult<Macro::EvaluateCallback> first_lookup = first.value->type->macro->evaluate_lookup(rest);

        switch (first_lookup.type()) {
        case lookup_result_match: throw CompileException("Evaluate not supported by " + first.value->type->macro->name());
        case lookup_result_conflict: throw CompileException("Evaluate not supported by " + first.value->type->macro->name());
        default: break;
        }

        return first_lookup.value()(first, rest, context, location);
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

          LookupResult<EvaluateContext::LookupCallback> first = context.lookup(bracket_operation);
          switch (first.type()) {
          case lookup_result_none:
            throw CompileException(str(boost::format("Context does not support evaluating %s brackets (%s operator missing)") % bracket_str % bracket_operation));
          case lookup_result_conflict:
            throw CompileException(str(boost::format("Context does not support evaluating %s brackets (conflict getting %s)") % bracket_str % bracket_operation));
          default: break;
          }

          DependentValue<TreePtr<> > first_result = first.value()(context, location);
          std::vector<boost::shared_ptr<Parser::Expression> > expression_list(1, expression);
          LookupResult<Macro::EvaluateCallback> first_lookup = first_result.value->type->macro->evaluate_lookup(expression_list);

          switch (first_lookup.type()) {
          case lookup_result_none:
            throw new CompileException(str(boost::format("Context does not support evaluating %s brackets (%s operator evaluation did not match)") % bracket_str % bracket_operation));
          case lookup_result_conflict:
            throw new CompileException(str(boost::format("Context does not support evaluating %s brackets (conflict on %s operator evaluation)") % bracket_str % bracket_operation));
          default: break;
          }

          return first_lookup.value()(first_result, expression_list, context, location);
        }

        case Parser::TokenExpression::identifier: {
          std::string name(token_expression.text.begin, token_expression.text.end);
          LookupResult<EvaluateContext::LookupCallback> result = context.lookup(name);

          switch (result.type()) {
          case lookup_result_none: throw CompileException("Name not found: " + name);
          case lookup_result_conflict: throw CompileException("Conflict on lookup of: " + name);
          default: break;
          }

          return result.value()(context, location);
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }
  }
}
