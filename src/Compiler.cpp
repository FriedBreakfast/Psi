#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

#include "Compiler.hpp"
#include "Tree.hpp"
#include "TreePattern.hpp"
#include "Platform.hpp"

namespace Psi {
  namespace Compiler {
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
      
      *m_error_stream << boost::format("%s:%s: in '%s'\n") % *loc.physical.url % loc.physical.first_line % logical_location_name(loc.logical);
      *m_error_stream << boost::format("%s:%s: %s:%s\n") % *loc.physical.url % loc.physical.first_line % type % message;
    }

    void CompileContext::error_throw(const SourceLocation& loc, const std::string& message, unsigned flags) {
      error(loc, message, flags);
      throw CompileException();
    }

    /**
     * \brief Create a tree for a global from the address of that global.
     * 
     */
    TreePtr<GlobalTree> tree_from_address(CompileContext& compile_context, const SourceLocation& location, const TreePtr<Type>& type, void *ptr) {
      void *base;
      String name;
      try {
        name = Platform::address_to_symbol(ptr, &base);
      } catch (Platform::PlatformError& e) {
        compile_context.error_throw(location, boost::format("Internal error: failed to get symbol name from addres: %s") % e.what());
      }
      if (base != ptr)
        compile_context.error_throw(location, "Internal error: address used to retrieve symbol did not match symbol base");

      TreePtr<ExternalGlobalTree> result(new GlobalTree(type));
      result->symbol_name = name;
      return result;
    }
    
    class EvaluateContextDictionaryTree : public Tree {
      virtual void gc_visit(GCVisitor& visitor) {
        for (NameMapType::iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
          visitor.visit_ptr(ii->second);
      }

    public:
      typedef std::map<String, TreePtr<> > NameMapType;
      NameMapType entries;
    };
    
    class EvaluateContextDictionary {
    public:
      TreePtr<> lookup(const TreePtr<>& data, const String& name) {
        TreePtr<EvaluateContextDictionaryTree> cast_data = checked_pointer_cast<EvaluateContextDictionaryTree>(data);
        EvaluateContextDictionaryTree::NameMapType::const_iterator it = cast_data->entries.find(name);
        if (it != cast_data->entries.end()) {
          return LookupResult<TreePtr<> >::make_match(it->second);
        } else {
          return LookupResult<TreePtr<> >::make_none();
        }
      }
      
      static EvaluateContextWrapper<EvaluateContextDictionary> vtable;
    };
    
    EvaluteContextWrapper<EvaluateContextDictionary> EvaluateContextDictionary::vtable;

    EvaluateContextRef evaluate_context_dictionary(CompileContext& compile_context, const std::map<String, TreePtr<> >& entries) {
      TreePtr<CompileImplementation> impl(new CompileImplementation());
      impl->vtable = tree_from_address(TreePtr<Type>(), &EvaluateContextDictionary::vtable);
      impl->data.reset(new EvaluateContextDictionaryTree(entries));
      return EvaluateContextRef(impl);
    }
    
    EvaluateContextRef evaluate_context_dictionary(CompileContext& compile_context, const std::map<String, TreePtr<> >& entries, const GCPtr<EvaluateContext>& next) {
      return GCPtr<EvaluateContext>(new EvaluateContextDictionary(compile_context, entries, next));
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
    TreePtr<> compile_expression(const SharedPtr<Parser::Expression>& expression,
                                 CompileContext& compile_context,
                                 const GCPtr<EvaluateContext>& evaluate_context,
                                 const SharedPtr<LogicalSourceLocation>& source) {

      SourceLocation location(expression->location, source);

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        TreePtr<> first = compile_expression(macro_expression.elements.front(), compile_context, evaluate_context, source);
        ArrayList<SharedPtr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());

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

          LookupResult<TreePtr<> > first = evaluate_context->lookup(bracket_operation);
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

          ArrayList<SharedPtr<Parser::Expression> > expression_list(1, expression);
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
          LookupResult<TreePtr<> > result = evaluate_context->lookup(name);

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

        GCPtr<Tree> left = compile_expression(dot_expression.left, compile_context, evaluate_context, source);
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
      enum BuildState {
        build_not_started,
        build_running,
        build_done,
        build_failed
      };
      
      struct Parameters {
        BuildState state;
        TreePtr<Statement> statement;
        boost::shared_ptr<Parser::Expression> expression;
        boost::shared_ptr<LogicalSourceLocation> logical_location;
        bool anonymize_location;
      };

      std::vector<Parameters> m_parameters;
      TreePtr<Block> m_block;
      GCPtr<EvaluateContext> m_evaluate_context;
      
      void run() {
        bool failed = false;
        
        // Build statements
        for (unsigned ii = 0, ie = m_parameters.size(); ii != ie; ++ii) {
          try {
            build_one(ii);
          } catch (CompileException&) {
            failed = true;
          }
        }

        // Link statements together
        TreePtr<Statement> *next_statement_ptr = &m_block->statements;
        for (std::vector<Parameters>::iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii) {
          *next_statement_ptr = ii->statement;
          next_statement_ptr = &ii->statement->next;
        }

        // Run dependent code
        for (std::vector<Parameters>::iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii) {
          try {
            if (ii->statement && ii->statement->dependency)
              ii->statement->dependency->dependency_call();
          } catch (CompileException&) {
            failed = true;
          }
        }

        // help the gc
        m_block.reset();
        m_evaluate_context.reset();

        if (failed)
          throw CompileException();
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
      
      TreePtr<Statement> build_one(unsigned index) {
        Parameters& params = m_parameters[index];
        switch (params.state) {
        case build_not_started: {
          boost::shared_ptr<Parser::Expression> expression;
          boost::shared_ptr<LogicalSourceLocation> logical_location;
          
          expression.swap(params.expression);
          logical_location.swap(params.logical_location);

          params.state = build_running;
          try {
            params.statement.reset(new Statement(compile_context()));
            TreePtr<> expr = compile_expression(expression, compile_context(), m_evaluate_context, logical_location, params.anonymize_location);
            params.statement->value = expr;
            params.statement->dependency = expr->dependency;
            params.statement->type = expr->type;
          } catch (...) {
            params.state = build_failed;
            throw;
          }
          params.state = build_done;

          return params.statement;
        }

        case build_running:
          compile_context().error_throw(SourceLocation(params.expression->location, params.logical_location), "Circular dependency during block compilation");

        case build_done:
          return params.statement;

        case build_failed:
          throw CompileException();

        default: PSI_FAIL("unreachable");
        }
      }

      static TreePtr<Block> make(const std::vector<boost::shared_ptr<Parser::NamedExpression> >&, CompileContext&, const GCPtr<EvaluateContext>&, const SourceLocation&);
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

      virtual LookupResult<TreePtr<> > lookup(const std::string& name) {
        NameMapType::const_iterator it = m_names.find(name);
        if (it != m_names.end()) {
          return LookupResult<TreePtr<> >::make_match(m_compiler->build_one(it->second));
        } else if (m_next) {
          return m_next->lookup(name);
        } else {
          return LookupResult<TreePtr<> >::make_none();
        }
      }
    };

    TreePtr<Block> StatementListCompiler::make(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements,
                                               CompileContext& compile_context,
                                               const GCPtr<EvaluateContext>& evaluate_context,
                                               const SourceLocation& location) {
      std::map<std::string, unsigned> names;

      GCPtr<StatementListCompiler> compiler(new StatementListCompiler(compile_context, location));

      for (std::vector<boost::shared_ptr<Parser::NamedExpression> >::const_iterator ii = statements.begin(), ib = statements.begin(), ie = statements.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        if (named_expr.expression) {
          Parameters parameters;
          parameters.state = build_not_started;
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

      TreePtr<Block> block(new Block(compile_context));
      block->dependency = compiler;
      compiler->m_block = block;
      compiler->m_evaluate_context.reset(new StatementListEvaluateContext(compile_context, evaluate_context, compiler, names));

      return block;
    }

    TreePtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements,
                                          CompileContext& compile_context,
                                          const GCPtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
      return StatementListCompiler::make(statements, compile_context, evaluate_context, location);
    }
  }
}
