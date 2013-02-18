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
        PSI_STD::vector<SharedPtr<Parser::Expression> > parameters_copy(macro_expression.parameters);

        return expression_macro(evaluate_context, first, location)->evaluate(first, list_from_stl(parameters_copy), evaluate_context, location);
      }

      case Parser::expression_dot: {
        const Parser::DotExpression& dot_expression = checked_cast<Parser::DotExpression&>(*expression);
        TreePtr<Term> obj = compile_expression(dot_expression.object, evaluate_context, source);
        PSI_STD::vector<SharedPtr<Parser::Expression> > parameters_copy(dot_expression.parameters);
        return expression_macro(evaluate_context, obj, location)->dot(obj, dot_expression.member, list_from_stl(parameters_copy), evaluate_context, location);
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

          boost::array<SharedPtr<Parser::Expression>, 1> expression_list;
          expression_list[0] = expression;
          return expression_macro(evaluate_context, first.value(), location)->evaluate(first.value(), list_from_stl(expression_list), evaluate_context, location);
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

          boost::array<SharedPtr<Parser::Expression>, 1> expression_list;
          expression_list[0] = expression;
          return expression_macro(evaluate_context, first.value(), location)->evaluate(first.value(), list_from_stl(expression_list), evaluate_context, location);
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class StatementListEntry {
      SharedPtr<Parser::Expression> m_expression;
      StatementMode m_mode;
      TreePtr<EvaluateContext> m_evaluate_context;

    public:
      StatementListEntry(const SharedPtr<Parser::Expression>& expression,
                         StatementMode mode,
                         const TreePtr<EvaluateContext>& evaluate_context)
        : m_expression(expression),
        m_mode(mode),
        m_evaluate_context(evaluate_context) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("expression", &StatementListEntry::m_expression)
        ("mode", &StatementListEntry::m_mode)
        ("evaluate_context", &StatementListEntry::m_evaluate_context);
      }

      TreePtr<Statement> evaluate(const TreePtr<Statement>& self) {
        TreePtr<Term> value = compile_expression(m_expression, m_evaluate_context, self.location().logical);
        StatementMode mode;
        if (value->is_type()) {
          if (m_mode != statement_mode_value)
            self.compile_context().error_throw(self.location(), "Types must have the default storage mode.");
          mode = statement_mode_functional;
        } else if (tree_isa<FunctionType>(value->type)) {
          if (m_mode != statement_mode_value)
            self.compile_context().error_throw(self.location(), "Functions must have the default storage mode.");
          mode = statement_mode_ref;
        } else {
          mode = m_mode;
        }
        
        return TreePtr<Statement>(new Statement(value, mode, self.location()));
      }
    };
    
    class BlockCompileData : public Tree {
    public:
      static const TreeVtable vtable;

      typedef std::map<String, TreePtr<Term> > NameMapType;

      BlockCompileData(const PSI_STD::vector<TreePtr<Statement> >& entries_, TreePtr<Term>& block_value_, const NameMapType& named_entries_, CompileContext& compile_context, const SourceLocation& location)
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
        v("entries", &BlockCompileData::entries)
        ("block_value", &BlockCompileData::block_value)
        ("named_entries", &BlockCompileData::named_entries);
      }
    };
    
    const TreeVtable BlockCompileData::vtable = PSI_COMPILER_TREE(BlockCompileData, "psi.compiler.BlockCompileData", Tree);

    class BlockContext : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef BlockCompileData::NameMapType NameMapType;
      
      BlockContext(const TreePtr<BlockCompileData>& statement_list_,
                           const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(&vtable, next_->module(), statement_list_.location()),
      statement_list(statement_list_),
      next(next_) {
      }

      TreePtr<BlockCompileData> statement_list;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("statement_list", &BlockContext::statement_list)
        ("next", &BlockContext::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const BlockContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        BlockContext::NameMapType::const_iterator it = self.statement_list->named_entries.find(name);
        if (it != self.statement_list->named_entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
      
      static void overload_list_impl(const BlockContext& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.next)
          self.next->overload_list(overload_type, overload_list);
      }
    };

    const EvaluateContextVtable BlockContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(BlockContext, "psi.compiler.BlockContext", EvaluateContext);

    class BlockCompiler {
      PSI_STD::vector<SharedPtr<Parser::Statement> > m_statements;
      TreePtr<EvaluateContext> m_evaluate_context;

    public:
      BlockCompiler(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                    const TreePtr<EvaluateContext>& evaluate_context)
      : m_statements(statements), m_evaluate_context(evaluate_context) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("statements", &BlockCompiler::m_statements)
        ("evaluate_context", &BlockCompiler::m_evaluate_context);
      }

      TreePtr<BlockCompileData> evaluate(const TreePtr<BlockCompileData>& self) {
        CompileContext& compile_context = self.compile_context();
        TreePtr<BlockContext> context_tree(new BlockContext(self, m_evaluate_context));
        TreePtr<StatementRef> last_statement;
        PSI_STD::vector<TreePtr<Statement> > entries;
        BlockCompileData::NameMapType named_entries;

        for (PSI_STD::vector<SharedPtr<Parser::Statement> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii) {
          if ((*ii) && (*ii)->expression) {
            const Parser::Statement& named_expr = **ii;
            String expr_name;
            LogicalSourceLocationPtr logical_location;
            if (named_expr.name) {
              expr_name = String(named_expr.name->begin, named_expr.name->end);
              logical_location = self.location().logical->named_child(expr_name);
            } else {
              logical_location = self.location().logical->new_anonymous_child();
            }
            SourceLocation statement_location(named_expr.location.location, logical_location);
            TreePtr<Statement> statement = tree_callback<Statement>(compile_context, statement_location, StatementListEntry(named_expr.expression, (StatementMode)named_expr.mode, context_tree));
            entries.push_back(statement);
            last_statement.reset(new StatementRef(statement, statement.location()));

            if (named_expr.name)
              named_entries[expr_name] = last_statement;
          } else {
            last_statement.reset();
          }
        }

        TreePtr<Term> block_value;
        if (last_statement) {
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
        
        return TreePtr<BlockCompileData>(new BlockCompileData(entries, block_value, named_entries, self.compile_context(), self.location()));
      }
    };

    TreePtr<Block> compile_block(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                                 const TreePtr<EvaluateContext>& evaluate_context,
                                 const SourceLocation& location) {
      TreePtr<BlockCompileData> t = tree_callback<BlockCompileData>(evaluate_context.compile_context(), location, BlockCompiler(statements, evaluate_context));
      return TreePtr<Block>(new Block(t->entries, t->block_value, location));
    }

    /**
     * Utility function to compile contents of different bracket types as a sequence of statements.
     */
    TreePtr<Block> compile_from_bracket(const SharedPtr<Parser::TokenExpression>& expr,
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
      SharedPtr<Parser::Expression> m_expression;
      StatementMode m_mode;
      TreePtr<EvaluateContext> m_evaluate_context;

    public:
      typedef Term TreeResultType;
      
      NamespaceEntry(const SharedPtr<Parser::Expression>& expression, StatementMode mode, const TreePtr<EvaluateContext>& evaluate_context)
        : m_expression(expression),
        m_mode(mode),
        m_evaluate_context(evaluate_context) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("expression", &NamespaceEntry::m_expression)
        ("evaluate_context", &NamespaceEntry::m_evaluate_context);
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& self) {
        TreePtr<Term> value = compile_expression(m_expression, m_evaluate_context, self.location().logical);
        if (value->is_type()) {
          if (m_mode != statement_mode_value)
            self.compile_context().error_throw(self.location(), "Types must have the default storage mode.");
          return TreePtr<GlobalDefine>(new GlobalDefine(value, true, self.location()));
        } else if (tree_isa<FunctionType>(value->type)) {
          if (m_mode != statement_mode_value)
            self.compile_context().error_throw(self.location(), "Functions must have the default storage mode.");
          return TreePtr<GlobalDefine>(new GlobalDefine(value, false, self.location()));
        } else {
          switch (m_mode) {
          case statement_mode_ref:
            return TreePtr<GlobalDefine>(new GlobalDefine(value, false, self.location()));
          case statement_mode_functional:
            return TreePtr<GlobalDefine>(new GlobalDefine(value, true, self.location()));
          case statement_mode_value:
            return TreePtr<GlobalVariable>(new GlobalVariable(m_evaluate_context->module(), false, value, false, false, self.location()));
          default:
            PSI_FAIL("Unrecognised namespace entry mode");
          }
        }
      }
    };
    
    class NamespaceContext : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef BlockCompileData::NameMapType NameMapType;
      
      NamespaceContext(const TreePtr<Namespace>& ns_, const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(&vtable, next_->module(), ns_.location()),
      ns(ns_),
      next(next_) {
      }

      TreePtr<Namespace> ns;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("namespace", &NamespaceContext::ns)
        ("next", &NamespaceContext::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const NamespaceContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        BlockContext::NameMapType::const_iterator it = self.ns->members.find(name);
        if (it != self.ns->members.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
      
      static void overload_list_impl(const NamespaceContext& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.next)
          self.next->overload_list(overload_type, overload_list);
      }
    };

    const EvaluateContextVtable NamespaceContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(NamespaceContext, "psi.compiler.NamespaceContext", EvaluateContext);

    class NamespaceCompiler {
      PSI_STD::vector<SharedPtr<Parser::Statement> > m_statements;
      TreePtr<EvaluateContext> m_evaluate_context;

    public:
      NamespaceCompiler(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                        const TreePtr<EvaluateContext>& evaluate_context)
      : m_statements(statements), m_evaluate_context(evaluate_context) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("statements", &NamespaceCompiler::m_statements)
        ("evaluate_context", &NamespaceCompiler::m_evaluate_context);
      }

      TreePtr<Namespace> evaluate(const TreePtr<Namespace>& self) {
        CompileContext& compile_context = self.compile_context();
        TreePtr<NamespaceContext> context_tree(new NamespaceContext(self, m_evaluate_context));
        Namespace::NameMapType named_entries;

        for (PSI_STD::vector<SharedPtr<Parser::Statement> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii) {
          if (*ii) {
            const Parser::Statement& named_expr = **ii;
            PSI_ASSERT(named_expr.expression && named_expr.name && (named_expr.mode != statement_mode_destroy));
            
            String expr_name(named_expr.name->begin, named_expr.name->end);
            LogicalSourceLocationPtr logical_location = self.location().logical->named_child(expr_name);
            SourceLocation entry_location(named_expr.location.location, logical_location);
            named_entries[expr_name] = tree_callback(compile_context, entry_location, NamespaceEntry(named_expr.expression, (StatementMode)named_expr.mode, context_tree));
          }
        }

        return TreePtr<Namespace>(new Namespace(self.compile_context(), named_entries, self.location()));
      }
    };

    TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) {
      return tree_callback<Namespace>(evaluate_context.compile_context(), location, NamespaceCompiler(statements, evaluate_context));
    }
  }
}
