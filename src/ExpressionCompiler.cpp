#include "Compiler.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "TermBuilder.hpp"

#include <boost/next_prior.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    /**
     * Get the Macro tree associated with an expression.
     */
    TreePtr<Macro> expression_macro(const TreePtr<EvaluateContext>& context, const TreePtr<Term>& expr, const SourceLocation& location) {
      return metadata_lookup_as<Macro>(expr.compile_context().builtins().macro_tag, context, expr, location);
    }
    
    std::pair<const char*, const char*> bracket_token_strings(Parser::TokenExpressionType type) {
      switch (type) {
      case Parser::token_bracket: return std::make_pair("__bracket__", "(...)");
      case Parser::token_brace: return std::make_pair("__brace__", "{...}");
      case Parser::token_square_bracket: return std::make_pair("__squareBracket__", "[...]");
      default: PSI_FAIL("unreachable");
      }
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
      case Parser::expression_evaluate: {
        const Parser::EvaluateExpression& macro_expression = checked_cast<Parser::EvaluateExpression&>(*expression);
        TreePtr<Term> first = compile_expression(macro_expression.object, evaluate_context, source->new_anonymous_child());
        return expression_macro(evaluate_context, first, location)->evaluate(first, macro_expression.parameters, evaluate_context, location);
      }

      case Parser::expression_dot: {
        const Parser::DotExpression& dot_expression = checked_cast<Parser::DotExpression&>(*expression);
        TreePtr<Term> obj = compile_expression(dot_expression.object, evaluate_context, source);
        return expression_macro(evaluate_context, obj, location)->dot(obj, dot_expression.member, dot_expression.parameters, evaluate_context, location);
      }

      case Parser::expression_token: {
        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*expression);

        switch (token_expression.token_type) {
        case Parser::token_bracket:
        case Parser::token_brace:
        case Parser::token_square_bracket: {
          std::pair<const char*, const char*> bracket_type = bracket_token_strings(token_expression.token_type);

          LookupResult<TreePtr<Term> > first = evaluate_context->lookup(bracket_type.first, location);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator missing") % bracket_type.second % bracket_type.first);
          case lookup_result_type_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator lookup ambiguous") % bracket_type.second % bracket_type.first);
          default: break;
          }

          if (!first.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successful lookup of '%s' returned NULL value") % bracket_type.second % bracket_type.first, CompileError::error_internal);

          PSI_STD::vector<SharedPtr<Parser::Expression> > expression_list(1, expression);
          return expression_macro(evaluate_context, first.value(), location)->evaluate(first.value(), expression_list, evaluate_context, location);
        }

        case Parser::token_identifier: {
          String name = token_expression.text.to_string();
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
        
        case Parser::token_number: {
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

          PSI_STD::vector<SharedPtr<Parser::Expression> > expression_list(1, expression);
          return expression_macro(evaluate_context, first.value(), location)->evaluate(first.value(), expression_list, evaluate_context, location);
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }
    
    StatementMode statement_mode(StatementMode base_mode, const TreePtr<Term>& value, const SourceLocation& location) {
      switch (base_mode) {
      case statement_mode_destroy:
        return statement_mode_destroy;
      
      case statement_mode_value:
        // Make type declarations functional without the user writing '::'
        if (value->is_type() && value->is_functional())
          return statement_mode_functional;
        else
          return statement_mode_value;
        
      case statement_mode_functional:
        return statement_mode_functional;
        
      case statement_mode_ref:
        switch (value->result_info().mode) {
        case result_mode_lvalue:
        case result_mode_rvalue:
          return statement_mode_ref;
          
        case result_mode_by_value: value.compile_context().error_throw(location, "Cannot create reference to temporary");
        case result_mode_functional: value.compile_context().error_throw(location, "Cannot create reference to functional value");
        default: PSI_FAIL("unexpected enum value");
        }
        
      default: PSI_FAIL("unexpected enum value");
      }
    }
    
    class BlockEntryCallback {
      SourceLocation m_location;
      SharedPtr<Parser::Expression> m_statement;
      StatementMode m_mode;
      
    public:
      BlockEntryCallback(const SourceLocation& location, const SharedPtr<Parser::Expression>& statement, StatementMode mode)
      : m_location(location), m_statement(statement), m_mode(mode) {
      }
      
      TreePtr<Statement> evaluate(const TreePtr<EvaluateContext>& context) {
        TreePtr<Term> value = compile_expression(m_statement, context, m_location.logical);
        return TermBuilder::statement(value, statement_mode(m_mode, value, m_location), m_location);
      }
      
      template<typename V>
      static void visit(V& v) {
        v("location", &BlockEntryCallback::m_location)
        ("statement", &BlockEntryCallback::m_statement)
        ("mode", &BlockEntryCallback::m_mode);
      }
    };
    
    class BlockContext : public EvaluateContext {
      typedef DelayedValue<TreePtr<Statement>, TreePtr<EvaluateContext> > StatementValueType;
      TreePtr<EvaluateContext> m_next;
      PSI_STD::vector<StatementValueType> m_statements;
      typedef PSI_STD::map<String, std::size_t> NameMapType;
      NameMapType m_names;
      bool m_has_last;
      
      TreePtr<EvaluateContext> get_ptr() const {return tree_from(this);}

    public:
      static const EvaluateContextVtable vtable;

      BlockContext(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                   const TreePtr<EvaluateContext>& next,
                   const SourceLocation& location)
      : EvaluateContext(&vtable, next->module(), location),
      m_next(next) {
        std::size_t index = 0;
        m_has_last = false;
        for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = statements.begin(), ie = statements.end(); ii != ie; ++ii) {
          if ((*ii) && (*ii)->expression) {
            const Parser::Statement& named_expr = **ii;
            LogicalSourceLocationPtr logical_location;
            if (named_expr.name) {
              String expr_name(named_expr.name->begin, named_expr.name->end);
              m_names.insert(std::make_pair(expr_name, index));
              logical_location = location.logical->named_child(expr_name);
            } else {
              logical_location = location.logical->new_anonymous_child();
            }
            SourceLocation statement_location(named_expr.location.location, logical_location);
            m_statements.push_back(StatementValueType(compile_context(), statement_location,
                                                      BlockEntryCallback(statement_location, named_expr.expression, (StatementMode)named_expr.mode)));
            m_has_last = true;
            ++index;
          } else {
            m_has_last = false;
          }
        }
      }
      
      bool has_last() const {return m_has_last;}
      
      PSI_STD::vector<TreePtr<Statement> > statements() const {
        PSI_STD::vector<TreePtr<Statement> > result;
        for (PSI_STD::vector<StatementValueType>::const_iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii)
          result.push_back(ii->get(this, &BlockContext::get_ptr));
        return result;
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("statement_list", &BlockContext::m_statements)
        ("names", &BlockContext::m_names)
        ("next", &BlockContext::m_next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const BlockContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        NameMapType::const_iterator it = self.m_names.find(name);
        if (it != self.m_names.end()) {
          return lookup_result_match(self.m_statements[it->second].get(&self, &BlockContext::get_ptr));
        } else if (self.m_next) {
          return self.m_next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
      
      static void overload_list_impl(const BlockContext& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.m_next)
          self.m_next->overload_list(overload_type, overload_list);
      }

      template<typename Derived>
      static void complete_impl(Derived& self, VisitQueue<TreePtr<> >& queue) {
        for (PSI_STD::vector<StatementValueType>::iterator ii = self.m_statements.begin(), ie = self.m_statements.end(); ii != ie; ++ii)
          ii->get(&self, &BlockContext::get_ptr);
        
        Tree::complete_impl(self, queue);
      }
    };

    const EvaluateContextVtable BlockContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(BlockContext, "psi.compiler.BlockContext", EvaluateContext);

    TreePtr<Term> compile_block(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                                const TreePtr<EvaluateContext>& evaluate_context,
                                const SourceLocation& location) {
      TreePtr<BlockContext> block_context(::new BlockContext(statements, evaluate_context, location));
      PSI_STD::vector<TreePtr<Statement> > block_statements = block_context->statements();
      TreePtr<Term> result;
      if (block_context->has_last())
        result = block_statements.back();
      else
        result = TermBuilder::empty_value(evaluate_context.compile_context());
      return TermBuilder::block(block_statements, result, location);
    }

    /**
     * Utility function to compile contents of different bracket types as a sequence of statements.
     */
    TreePtr<Term> compile_from_bracket(const SharedPtr<Parser::TokenExpression>& expr,
                                       const TreePtr<EvaluateContext>& evaluate_context,
                                       const SourceLocation& location) {
      PSI_STD::vector<SharedPtr<Parser::Statement> > statements;
      try {
        statements = Parser::parse_statement_list(expr->text);
      } catch (Parser::ParseError& ex) {
        SourceLocation error_loc = location.relocate(ex.location());
        evaluate_context.compile_context().error_throw(error_loc, ex.what());
      }
      return compile_block(statements, evaluate_context, location);
    }

    class NamespaceEntry {
      SharedPtr<Parser::Expression> m_statement;
      StatementMode m_mode;
      SourceLocation m_location;

    public:
      typedef Term TreeResultType;
      
      NamespaceEntry(const SharedPtr<Parser::Expression>& expression, StatementMode mode, const SourceLocation& location)
      : m_statement(expression),
      m_mode(mode),
      m_location(location) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("expression", &NamespaceEntry::m_statement)
        ("mode", &NamespaceEntry::m_mode)
        ("location", &NamespaceEntry::m_location);
      }

      TreePtr<Term> evaluate(const TreePtr<EvaluateContext>& context) {
        TreePtr<Term> value = compile_expression(m_statement, context, m_location.logical);
        return TermBuilder::global_statement(context->module(), value, statement_mode(m_mode, value, m_location), m_location);
      }
    };
    
    class NamespaceContext : public EvaluateContext {
      typedef DelayedValue<TreePtr<Term>, TreePtr<EvaluateContext> > EntryType;
      typedef PSI_STD::map<String, EntryType> NameMapType;
      NameMapType m_entries;
      TreePtr<EvaluateContext> m_next;
      
      TreePtr<EvaluateContext> get_ptr() const {return tree_from(this);}

    public:
      static const EvaluateContextVtable vtable;

      NamespaceContext(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                       const TreePtr<EvaluateContext>& next,
                       const SourceLocation& location)
      : EvaluateContext(&vtable, next->module(), location),
      m_next(next) {
        for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = statements.begin(), ie = statements.end(); ii != ie; ++ii) {
          if (*ii) {
            const Parser::Statement& named_expr = **ii;
            PSI_ASSERT(named_expr.expression && named_expr.name && (named_expr.mode != statement_mode_destroy));
            
            String expr_name(named_expr.name->begin, named_expr.name->end);
            LogicalSourceLocationPtr logical_location = location.logical->named_child(expr_name);
            SourceLocation entry_location(named_expr.location.location, logical_location);
            m_entries.insert(std::make_pair(expr_name, EntryType(compile_context(), entry_location,
                                                                 NamespaceEntry(named_expr.expression, (StatementMode)named_expr.mode, entry_location))));
          }
        }
      }
      
      Namespace::NameMapType names() const {
        Namespace::NameMapType result;
        for (NameMapType::const_iterator ii = m_entries.begin(), ie = m_entries.end(); ii != ie; ++ii)
          result.insert(std::make_pair(ii->first, ii->second.get(this, &NamespaceContext::get_ptr)));
        return result;
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("entries", &NamespaceContext::m_entries)
        ("next", &NamespaceContext::m_next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const NamespaceContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        NameMapType::const_iterator it = self.m_entries.find(name);
        if (it != self.m_entries.end()) {
          return lookup_result_match(it->second.get(&self, &NamespaceContext::get_ptr));
        } else if (self.m_next) {
          return self.m_next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
      
      static void overload_list_impl(const NamespaceContext& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.m_next)
          self.m_next->overload_list(overload_type, overload_list);
      }

      template<typename Derived>
      static void complete_impl(Derived& self, VisitQueue<TreePtr<> >& queue) {
        for (NameMapType::iterator ii = self.m_entries.begin(), ie = self.m_entries.end(); ii != ie; ++ii)
          ii->second.get(&self, &NamespaceContext::get_ptr);
        
        Tree::complete_impl(self, queue);
      }
    };

    const EvaluateContextVtable NamespaceContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(NamespaceContext, "psi.compiler.NamespaceContext", EvaluateContext);

    TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) {
      TreePtr<NamespaceContext> nsc(::new NamespaceContext(statements, evaluate_context, location));
      return Namespace::new_(evaluate_context.compile_context(), nsc->names(), location);
    }
  }
}
