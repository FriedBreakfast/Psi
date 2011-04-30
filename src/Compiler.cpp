#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

#include "Compiler.hpp"
#include "Tree.hpp"
#include "TreePattern.hpp"

namespace Psi {
  namespace Compiler {
    CompileObject::CompileObject(CompileContext& compile_context)
    : m_compile_context(&compile_context) {
      compile_context.m_gc_pool.add(this);
    }

    CompileObject::~CompileObject() {
    }

    void CompileObject::gc_destroy() {
      delete this;
    }

    Future::Future(CompileContext& context, const SourceLocation& location)
    : CompileObject(context), m_state(state_constructed), m_location(location) {
    }

    Future::~Future() {
    }

    void Future::call() {
      switch (m_state) {
      case state_constructed: run_wrapper(); break;
      case state_running: throw_circular_exception();
      case state_finished: break;
      case state_failed: throw_failed_exception();
      default: PSI_FAIL("unknown future state");
      }
    }

    void Future::dependency_call() {
      switch (m_state) {
      case state_constructed: run_wrapper(); break;
      case state_running:
      case state_finished: break;
      case state_failed: throw_failed_exception();
      default: PSI_FAIL("unknown future state");
      }
    }

    void Future::run_wrapper() {
      try {
        m_state = state_running;
        run();
        m_state = state_finished;
      } catch (...) {
        m_state = state_failed;
        throw_failed_exception();
      }
    }

    void Future::throw_circular_exception() {
      compile_context().error_throw(m_location, "Circular dependency during code evaluation");
    }
    
    void Future::throw_failed_exception() {
      throw CompileException();
    }

    EvaluateContext::EvaluateContext(CompileContext& compile_context) : CompileObject(compile_context) {
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

    CompileContext::CompileContext(std::ostream *error_stream)
    : m_error_stream(error_stream), m_error_occurred(false) {

      m_empty_type.reset(new EmptyType(*this));
      m_empty_type->macro = make_interface(*this, "Empty");
    }

    CompileContext::~CompileContext() {
    }

    void CompileContext::error(const SourceLocation& loc, const std::string& message, unsigned flags) {
      const char *type;
      switch (flags) {
      case error_warning: type = "warning"; break;
      case error_internal: type = "internal error"; m_error_occurred = true; break;
      default: type = "error"; m_error_occurred = true; break;
      }
      
      *m_error_stream << boost::format("%s:%s: in '%s'\n") % loc.physical.origin->name() % loc.physical.first_line % loc.logical->full_name();
      *m_error_stream << boost::format("%s:%s: %s:%s\n") % loc.physical.origin->name() % loc.physical.first_line % type % message;
    }

    void CompileContext::error_throw(const SourceLocation& loc, const std::string& message, unsigned flags) {
      error(loc, message, flags);
      throw CompileException();
    }

    Macro::~Macro() {
    }

    LogicalSourceLocation::~LogicalSourceLocation() {
    }

    EvaluateCallback::EvaluateCallback(CompileContext& compile_context) : CompileObject(compile_context) {
    }
    
    EvaluateCallback::~EvaluateCallback() {
    }

    DotCallback::DotCallback(CompileContext& compile_context) : CompileObject(compile_context) {
    }

    DotCallback::~DotCallback() {
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
        if (m_parent) {
          std::string parent_name = m_parent->full_name();
          if (!parent_name.empty()) {
            parent_name += '.';
            parent_name += m_name;
            return parent_name;
          }
        }

        return m_name;
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

    class EvaluateContextDictionary : public EvaluateContext {
    public:
      typedef std::map<std::string, GCPtr<Tree> > NameMapType;
      NameMapType m_entries;
      GCPtr<EvaluateContext> m_next;

      virtual void gc_visit(GCVisitor& visitor) {
        visitor % m_next;
        for (NameMapType::iterator ii = m_entries.begin(), ie = m_entries.end(); ii != ie; ++ii)
          visitor.visit_ptr(ii->second);
      }

    public:
      EvaluateContextDictionary(CompileContext& compile_context, const std::map<std::string, GCPtr<Tree> >& entries, const GCPtr<EvaluateContext>& next)
      : EvaluateContext(compile_context), m_entries(entries), m_next(next) {
      }
      
      virtual LookupResult<GCPtr<Tree> > lookup(const std::string& name) {
        NameMapType::const_iterator it = m_entries.find(name);
        if (it != m_entries.end()) {
          return LookupResult<GCPtr<Tree> >::make_match(it->second);
        } else if (m_next) {
          return m_next->lookup(name);
        } else {
          return LookupResult<GCPtr<Tree> >::make_none();
        }
      }
    };

    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext& compile_context, const std::map<std::string, GCPtr<Tree> >& entries, const GCPtr<EvaluateContext>& next) {
      return GCPtr<EvaluateContext>(new EvaluateContextDictionary(compile_context, entries, next));
    }
    
    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext& compile_context, const std::map<std::string, GCPtr<Tree> >& entries) {
      return evaluate_context_dictionary(compile_context, entries, GCPtr<EvaluateContext>());
    }

    /**
     * \brief OutputStreamable class which can be used to turn an expression into a simple string.
     */
    class ExpressionString {
      PhysicalSourceLocation m_location;

    public:
      ExpressionString(const boost::shared_ptr<Parser::Expression>& expr) : m_location(expr->location) {
      }
      
      friend std::ostream& operator << (std::ostream& os, const ExpressionString& self) {
        os.write(self.m_location.begin, self.m_location.end - self.m_location.begin);
        return os;
      }
    };
    
    /**
     * \brief Compile an expression.
     *
     * \param expression Expression, usually as produced by the parser.
     * \param compile_context Compilation context.
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     * \param anonymize_location Whether to generate a new, anonymous location as a child of the current location.
     */
    GCPtr<Tree> compile_expression(const boost::shared_ptr<Parser::Expression>& expression,
                                   CompileContext& compile_context,
                                   const GCPtr<EvaluateContext>& evaluate_context,
                                   const boost::shared_ptr<LogicalSourceLocation>& source,
                                   bool anonymize_location) {

      SourceLocation location(expression->location, source);
      boost::shared_ptr<LogicalSourceLocation> child_source = anonymize_location ? anonymous_child_location(source) : source;

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        GCPtr<Tree> first = compile_expression(macro_expression.elements.front(), compile_context, evaluate_context, child_source, false);
        std::vector<boost::shared_ptr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());

        if (!first->type)
          compile_context.error_throw(location, "Term does not have a type", CompileContext::error_internal);
        if (!first->type->macro)
          compile_context.error_throw(location, "Type does not have an associated macro", CompileContext::error_internal);
        
        LookupResult<GCPtr<EvaluateCallback> > first_lookup = first->type->macro->evaluate_lookup(rest);

        switch (first_lookup.type()) {
        case lookup_result_none: compile_context.error_throw(location, boost::format("No matching evaluation function for arguments to '%s'") % first->type->macro->name());
        case lookup_result_conflict: compile_context.error_throw(location, boost::format("Conflicting matching evaluation functions for arguments to '%s'") % first->type->macro->name());
        default: break;
        }

        if (!first_lookup.value())
            compile_context.error_throw(location, boost::format("Evaluate callback returned by '%s' was NULL") % first->type->macro->name(), CompileContext::error_internal);

        return first_lookup.value()->evaluate_callback(first, rest, compile_context, evaluate_context, location);
      }

      case Parser::expression_token: {
        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*expression);

        switch (token_expression.token_type) {
        case Parser::TokenExpression::bracket:
        case Parser::TokenExpression::brace:
        case Parser::TokenExpression::square_bracket: {
          const char *bracket_operation, *bracket_str;
          switch (token_expression.token_type) {
          case Parser::TokenExpression::bracket: bracket_operation = "__bracket__"; bracket_str = "(...)"; break;
          case Parser::TokenExpression::brace: bracket_operation = "__brace__"; bracket_str = "{...}"; break;
          case Parser::TokenExpression::square_bracket: bracket_operation = "__squareBracket__"; bracket_str = "[...]"; break;
          default: PSI_FAIL("unreachable");
          }

          LookupResult<GCPtr<Tree> > first = evaluate_context->lookup(bracket_operation);
          switch (first.type()) {
          case lookup_result_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator missing") % bracket_str % bracket_operation);
          case lookup_result_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator lookup ambiguous") % bracket_str % bracket_operation);
          default: break;
          }

          if (!first.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successful lookup of '%s' returned NULL value") % bracket_str % bracket_operation, CompileContext::error_internal);

          if (!first.value()->type)
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator does not have a type") % bracket_str % bracket_operation, CompileContext::error_internal);
          if (!first.value()->type->macro)
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator's type does not have an associated macro") % bracket_str % bracket_operation, CompileContext::error_internal);

          std::vector<boost::shared_ptr<Parser::Expression> > expression_list(1, expression);
          LookupResult<GCPtr<EvaluateCallback> > first_lookup = first.value()->type->macro->evaluate_lookup(expression_list);

          switch (first_lookup.type()) {
          case lookup_result_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator did not accept bracket contents") % bracket_str % bracket_operation);
          case lookup_result_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator bracket failed to accept bracket contents because they are ambiguous") % bracket_str % bracket_operation);
          default: break;
          }

          if (!first_lookup.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successfully matched evaluate callback on '%s' is NULL") % bracket_str % bracket_operation, CompileContext::error_internal);

          return first_lookup.value()->evaluate_callback(first.value(), expression_list, compile_context, evaluate_context, location);
        }

        case Parser::TokenExpression::identifier: {
          std::string name(token_expression.text.begin, token_expression.text.end);
          LookupResult<GCPtr<Tree> > result = evaluate_context->lookup(name);

          switch (result.type()) {
          case lookup_result_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
          default: break;
          }

          if (!result.value())
            compile_context.error_throw(location, boost::format("Successful lookup of '%s' returned NULL value") % name, CompileContext::error_internal);

          return result.value();
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      case Parser::expression_dot: {
        const Parser::DotExpression& dot_expression = checked_cast<Parser::DotExpression&>(*expression);

        GCPtr<Tree> left = compile_expression(dot_expression.left, compile_context, evaluate_context, child_source);
        LookupResult<GCPtr<DotCallback> > result = left->type->macro->dot_lookup(dot_expression.right);

        switch (result.type()) {
        case lookup_result_none: compile_context.error_throw(location, boost::format("Name not found: %s") % ExpressionString(dot_expression.right));
        case lookup_result_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % ExpressionString(dot_expression.right));
        default: break;
        }

        if (!result.value())
          compile_context.error_throw(location, boost::format("Successful member lookup of '%s' returned NULL value") % ExpressionString(dot_expression.right), CompileContext::error_internal);

        return result.value()->dot_callback(left, dot_expression.right, compile_context, evaluate_context, location);
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class StatementListCompiler : public Future {
      struct Parameters {
        GCPtr<Statement> statement;
        boost::shared_ptr<Parser::Expression> expression;
        boost::shared_ptr<LogicalSourceLocation> logical_location;
        bool anonymize_location;
      };

      std::vector<Parameters> m_parameters;
      GCPtr<Block> m_block;
      GCPtr<EvaluateContext> m_evaluate_context;
      
      void run() {
        // Build statements
        for (unsigned ii = 0, ie = m_parameters.size(); ii != ie; ++ii)
          build_one(ii);

        // Link statements together
        GCPtr<Statement> *next_statement_ptr = &m_block->statements;
        for (std::vector<Parameters>::iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii) {
          *next_statement_ptr = ii->statement;
          next_statement_ptr = &ii->statement->next;
        }

        // help the gc
        m_block.reset();
        m_evaluate_context.reset();
      }

      virtual void gc_visit(GCVisitor& visitor) {
        Future::gc_visit(visitor);
        visitor % m_block % m_evaluate_context;
        for (std::vector<Parameters>::iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii)
          visitor % ii->statement;
      }

    public:
      StatementListCompiler(CompileContext& compile_context, const SourceLocation& location)
      : Future(compile_context, location) {
      }
      
      GCPtr<Statement> build_one(unsigned index) {
        Parameters& params = m_parameters[index];
        if (!params.statement) {
          params.statement.reset(new Statement(compile_context()));
          GCPtr<Tree> expr = compile_expression(params.expression, compile_context(), m_evaluate_context, params.logical_location, params.anonymize_location);
          params.statement->value = expr;
          params.statement->dependency = expr->dependency;
          params.statement->type = expr->type;

          params.logical_location.reset();
          params.expression.reset();
        } else {
          if (!params.statement->value)
            compile_context().error_throw(SourceLocation(params.expression->location, params.logical_location), "Circular dependency during block compilation");
        }

        return params.statement;
      }

      static GCPtr<Block> make(const std::vector<boost::shared_ptr<Parser::NamedExpression> >&, CompileContext&, const GCPtr<EvaluateContext>&, const SourceLocation&);
    };

    class StatementListEvaluateContext : public EvaluateContext {
      typedef std::map<std::string, unsigned> NameMapType;
      GCPtr<EvaluateContext> m_next;
      GCPtr<StatementListCompiler> m_compiler;
      NameMapType m_names;

      virtual void gc_visit(GCVisitor& visitor) {
        visitor % m_next % m_compiler;
      }

    public:
      StatementListEvaluateContext(CompileContext& compile_context,
                                   const GCPtr<EvaluateContext>& next,
                                   const GCPtr<StatementListCompiler>& compiler,
                                   std::map<std::string, unsigned>& names)
      : EvaluateContext(compile_context), m_next(next), m_compiler(compiler) {
        m_names.swap(names);
      }

      virtual LookupResult<GCPtr<Tree> > lookup(const std::string& name) {
        NameMapType::const_iterator it = m_names.find(name);
        if (it != m_names.end()) {
          return LookupResult<GCPtr<Tree> >::make_match(m_compiler->build_one(it->second));
        } else if (m_next) {
          return m_next->lookup(name);
        } else {
          return LookupResult<GCPtr<Tree> >::make_none();
        }
      }
    };

    GCPtr<Block> StatementListCompiler::make(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements,
                                             CompileContext& compile_context,
                                             const GCPtr<EvaluateContext>& evaluate_context,
                                             const SourceLocation& location) {
      std::map<std::string, unsigned> names;

      GCPtr<StatementListCompiler> compiler(new StatementListCompiler(compile_context, location));

      for (std::vector<boost::shared_ptr<Parser::NamedExpression> >::const_iterator ii = statements.begin(), ib = statements.begin(), ie = statements.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        if (named_expr.expression) {
          Parameters parameters;
          parameters.expression = named_expr.expression;
          
          boost::shared_ptr<LogicalSourceLocation> statement_location;
          if (named_expr.name) {
            std::string expr_name(named_expr.name->begin, named_expr.name->end);
            names.insert(std::make_pair(expr_name, ii - ib));
            parameters.anonymize_location = true;
            parameters.logical_location = named_child_location(location.logical, expr_name);
          } else {
            parameters.anonymize_location = false;
            parameters.logical_location = anonymous_child_location(location.logical);
          }

          compiler->m_parameters.push_back(parameters);
        }
      }

      GCPtr<Block> block(new Block(compile_context));
      block->dependency = compiler;
      compiler->m_block = block;
      compiler->m_evaluate_context.reset(new StatementListEvaluateContext(compile_context, evaluate_context, compiler, names));

      return block;
    }

    GCPtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements,
                                        CompileContext& compile_context,
                                        const GCPtr<EvaluateContext>& evaluate_context,
                                        const SourceLocation& location) {
      return StatementListCompiler::make(statements, compile_context, evaluate_context, location);
    }
  }
}
