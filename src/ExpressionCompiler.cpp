#include "Compiler.hpp"
#include "Parser.hpp"
#include "Tree.hpp"

#include <boost/next_prior.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    /**
     * Get the Macro tree associated with an expression.
     */
    TreePtr<Macro> expression_macro(const TreePtr<Term>& expr, const SourceLocation& location) {
      return interface_lookup_as<Macro>(expr.compile_context().builtins().macro_interface, expr, location);
    }
    
    /**
     * \brief Compile an expression.
     *
     * \param expression Expression, usually as produced by the parser.
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     */
    TreePtr<Term> compile_expression(const SharedPtr<Parser::Expression>& expression,
                                     const TreePtr<EvaluateContext>& evaluate_context,
                                     const LogicalSourceLocationPtr& source) {

      CompileContext& compile_context = evaluate_context.compile_context();
      SourceLocation location(expression->location.location, source);

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        TreePtr<Term> first = compile_expression(macro_expression.elements.front(), evaluate_context, source->new_anonymous_child());
        PSI_STD::vector<SharedPtr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());

        return expression_macro(first, location)->evaluate(first, list_from_stl(rest), evaluate_context, location);
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

          LookupResult<TreePtr<Term> > first = evaluate_context->lookup(bracket_operation, location);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator missing") % bracket_str % bracket_operation);
          case lookup_result_type_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator lookup ambiguous") % bracket_str % bracket_operation);
          default: break;
          }

          if (!first.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successful lookup of '%s' returned NULL value") % bracket_str % bracket_operation, CompileError::error_internal);

          boost::array<SharedPtr<Parser::Expression>, 1> expression_list;
          expression_list[0] = expression;
          return expression_macro(first.value(), location)->evaluate(first.value(), list_from_stl(expression_list), evaluate_context, location);
        }

        case Parser::TokenExpression::identifier: {
          String name(token_expression.text.begin, token_expression.text.end);
          LookupResult<TreePtr<Term> > result = evaluate_context->lookup(name, location);

          switch (result.type()) {
          case lookup_result_type_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_type_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
          default: break;
          }

          if (!result.value())
            compile_context.error_throw(location, boost::format("Successful lookup of '%s' returned NULL value") % name, CompileError::error_internal);

          return result.value();
        }
        
        case Parser::TokenExpression::number: {
          LookupResult<TreePtr<Term> > first = evaluate_context->lookup("__number__", location);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, "Cannot evaluate number: '__number__' operator missing");
          case lookup_result_type_conflict:
            compile_context.error_throw(location, "Cannot evaluate number: '__number__' operator lookup ambiguous");
          default: break;
          }
          
          if (!first.value())
            compile_context.error_throw(location, "Cannot evaluate number: successful lookup of '__number__' returned NULL value", CompileError::error_internal);

          boost::array<SharedPtr<Parser::Expression>, 1> expression_list;
          expression_list[0] = expression;
          return expression_macro(first.value(), location)->evaluate(first.value(), list_from_stl(expression_list), evaluate_context, location);
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      case Parser::expression_dot: {
        const Parser::DotExpression& dot_expression = checked_cast<Parser::DotExpression&>(*expression);
        TreePtr<Term> left = compile_expression(dot_expression.left, evaluate_context, source);
        return expression_macro(left, location)->dot(left, dot_expression.right, evaluate_context, location);
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class StatementListEntry {
      SharedPtr<Parser::Expression> m_expression;
      TreePtr<EvaluateContext> m_evaluate_context;

    public:
      StatementListEntry(const SharedPtr<Parser::Expression>& expression,
                         const TreePtr<EvaluateContext>& evaluate_context)
        : m_expression(expression),
        m_evaluate_context(evaluate_context) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("expression", &StatementListEntry::m_expression)
        ("evaluate_context", &StatementListEntry::m_evaluate_context);
      }

      TreePtr<Statement> evaluate(const TreePtr<Statement>& self) {
        return TreePtr<Statement>(new Statement(compile_expression(m_expression, m_evaluate_context, self.location().logical), self.location()));
      }
    };
    
    class StatementListTree : public Tree {
    public:
      static const TreeVtable vtable;

      typedef std::map<String, TreePtr<Term> > NameMapType;

      StatementListTree(const PSI_STD::vector<TreePtr<Statement> >& entries_, TreePtr<Term>& block_value_, const NameMapType& named_entries_, CompileContext& compile_context, const SourceLocation& location)
      : Tree(&vtable, compile_context, location),
      entries(entries_),
      block_value(block_value_),
      named_entries(named_entries_) {
      }

      PSI_STD::vector<TreePtr<Statement> > entries;
      TreePtr<Term> block_value;
      NameMapType named_entries;

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("entries", &StatementListTree::entries)
        ("block_value", &StatementListTree::block_value)
        ("named_entries", &StatementListTree::named_entries);
      }
    };
    
    const TreeVtable StatementListTree::vtable = PSI_COMPILER_TREE(StatementListTree, "psi.compiler.StatementListTree", Tree);

    class StatementListContext : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef StatementListTree::NameMapType NameMapType;
      
      StatementListContext(const TreePtr<StatementListTree>& statement_list_,
                           const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(&vtable, next_->module(), statement_list_.location()),
      statement_list(statement_list_),
      next(next_) {
      }

      TreePtr<StatementListTree> statement_list;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("statement_list", &StatementListContext::statement_list)
        ("next", &StatementListContext::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const StatementListContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        StatementListContext::NameMapType::const_iterator it = self.statement_list->named_entries.find(name);
        if (it != self.statement_list->named_entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable StatementListContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(StatementListContext, "psi.compiler.StatementListContext", EvaluateContext);

    class StatementListCompiler {
      PSI_STD::vector<SharedPtr<Parser::NamedExpression> > m_statements;
      TreePtr<EvaluateContext> m_evaluate_context;
      bool m_with_value;

    public:
      StatementListCompiler(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements,
                            const TreePtr<EvaluateContext>& evaluate_context,
                            bool with_value)
      : m_statements(statements), m_evaluate_context(evaluate_context), m_with_value(with_value) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("statements", &StatementListCompiler::m_statements)
        ("evaluate_context", &StatementListCompiler::m_evaluate_context)
        ("with_value", &StatementListCompiler::m_with_value);
      }

      TreePtr<StatementListTree> evaluate(const TreePtr<StatementListTree>& self) {
        CompileContext& compile_context = self.compile_context();
        TreePtr<StatementListContext> context_tree(new StatementListContext(self, m_evaluate_context));
        TreePtr<Statement> last_statement;
        bool last_statement_set = false;
        PSI_STD::vector<TreePtr<Statement> > entries;
        StatementListTree::NameMapType named_entries;

        for (PSI_STD::vector<SharedPtr<Parser::NamedExpression> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii) {
          const Parser::NamedExpression& named_expr = **ii;
          if ((last_statement_set = named_expr.expression.get())) {
            String expr_name;
            LogicalSourceLocationPtr logical_location;
            if (named_expr.name) {
              expr_name = String(named_expr.name->begin, named_expr.name->end);
              logical_location = self.location().logical->named_child(expr_name);
            } else {
              logical_location = self.location().logical->new_anonymous_child();
            }
            SourceLocation statement_location(named_expr.location.location, logical_location);
            last_statement = tree_callback<Statement>(compile_context, statement_location, StatementListEntry(named_expr.expression, context_tree));
            entries.push_back(last_statement);

            if (named_expr.name)
              named_entries[expr_name] = last_statement;
          }
        }

        TreePtr<Term> block_value;
        if (m_with_value) {
          if (last_statement_set) {
            block_value = last_statement;
          } else {
            LookupResult<TreePtr<Term> > none = m_evaluate_context->lookup("__none__", self.location());
            switch (none.type()) {
            case lookup_result_type_none:
              compile_context.error_throw(self.location(), "'__none__' missing");
            case lookup_result_type_conflict:
              compile_context.error_throw(self.location(), "'__none__' has multiple definitions");
            default: break;
            }

            if (!none.value())
              compile_context.error_throw(self.location(), "'__none__' returned a NULL tree", CompileError::error_internal);

            block_value = none.value();
          }
        }
        
        return TreePtr<StatementListTree>(new StatementListTree(entries, block_value, named_entries, self.compile_context(), self.location()));
      }
    };

    TreePtr<Block> compile_statement_list(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements,
                                          const TreePtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
      TreePtr<StatementListTree> t = tree_callback<StatementListTree>(evaluate_context.compile_context(), location, StatementListCompiler(statements, evaluate_context, true));
      return TreePtr<Block>(new Block(t->entries, t->block_value, location));
    }
    
    NamespaceCompileResult compile_namespace(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements,
                                             const TreePtr<EvaluateContext>& evaluate_context,
                                             const SourceLocation& location) {
      TreePtr<StatementListTree> t = tree_callback<StatementListTree>(evaluate_context.compile_context(), location, StatementListCompiler(statements, evaluate_context, false));
      NamespaceCompileResult r;
      r.ns = TreePtr<Namespace>(new Namespace(t->entries, evaluate_context.compile_context(), location));
      r.entries = t->named_entries;
      return r;
    }
  }
}
