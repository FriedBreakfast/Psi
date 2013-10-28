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
    TreePtr<Macro> expression_macro(const TreePtr<EvaluateContext>& context, const TreePtr<Term>& expr, const TreePtr<Term>& tag_type, const SourceLocation& location) {
      CompileContext& compile_context = context->compile_context();
      if (!expr->type) {
        return metadata_lookup_as<Macro>(compile_context.builtins().metatype_macro, context, tag_type, location);
      } else {
        PSI_STD::vector<TreePtr<Term> > args(2);
        args[0] = expr->is_type() ? expr : expr->type;
        args[1] = tag_type;
        const TreePtr<MetadataType>& md_type = expr->is_type() ? compile_context.builtins().type_macro
          : compile_context.builtins().macro;
        return metadata_lookup_as<Macro>(md_type, context, args, location);
      }
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
     * \param mode_tag Tag type which identifies the sort of result expected and the argument type passed.
     * \param arg Auxiliary data.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     */
    void compile_expression(void *result,
                            const SharedPtr<Parser::Expression>& expression,
                            const TreePtr<EvaluateContext>& evaluate_context,
                            const TreePtr<Term>& mode_tag,
                            const void *arg,
                            const LogicalSourceLocationPtr& source) {

      CompileContext& compile_context = evaluate_context->compile_context();
      SourceLocation location(expression->location, source);

      switch (expression->expression_type) {
      case Parser::expression_evaluate: {
        const Parser::EvaluateExpression& macro_expression = checked_cast<Parser::EvaluateExpression&>(*expression);
        TreePtr<Term> first = compile_term(macro_expression.object, evaluate_context, source);
        expression_macro(evaluate_context, first, mode_tag, location)->evaluate_raw(result, first, macro_expression.parameters, evaluate_context, arg, location);
        return;
      }

      case Parser::expression_dot: {
        const Parser::DotExpression& dot_expression = checked_cast<Parser::DotExpression&>(*expression);
        TreePtr<Term> obj = compile_term(dot_expression.object, evaluate_context, source);
        expression_macro(evaluate_context, obj, mode_tag, location)->dot_raw(result, obj, dot_expression.member, dot_expression.parameters, evaluate_context, arg, location);
        return;
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
          expression_macro(evaluate_context, first.value(), mode_tag, location)->evaluate_raw(result, first.value(), expression_list, evaluate_context, arg, location);
          return;
        }

        case Parser::token_identifier: {
          String name = token_expression.text.str();
          LookupResult<TreePtr<Term> > id = evaluate_context->lookup(name, location);

          switch (id.type()) {
          case lookup_result_type_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_type_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
          default: break;
          }

          if (!id.value())
            compile_context.error_throw(location, boost::format("Successful lookup of '%s' returned NULL value") % name, CompileError::error_internal);

          expression_macro(evaluate_context, id.value(), mode_tag, location)->cast_raw(result, id.value(), evaluate_context, arg, location);
          return;
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
          expression_macro(evaluate_context, first.value(), mode_tag, location)->evaluate_raw(result, first.value(), expression_list, evaluate_context, arg, location);
          return;
        }

        default:
          PSI_FAIL("Unknown token type");
        }
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }
    
    /**
     * \brief Compile an expression to a term
     * 
     * \param expression Expression, usually as produced by the parser.
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     */
    TreePtr<Term> compile_term(const SharedPtr<Parser::Expression>& expression,
                               const TreePtr<EvaluateContext>& evaluate_context,
                               const LogicalSourceLocationPtr& source) {
      return compile_expression<TreePtr<Term> >(expression, evaluate_context, evaluate_context->compile_context().builtins().macro_term_tag, MacroTermArgument(), source);
    }
    
    /**
     * \brief Compile an interface constraint description.
     */
    TreePtr<InterfaceValue> compile_interface_value(const SharedPtr<Parser::Expression>& expression,
                                                    const TreePtr<EvaluateContext>& evaluate_context,
                                                    const LogicalSourceLocationPtr& source) {
      /// \todo Create an interface value constraint macro
      TreePtr<Term> interface = compile_term(expression, evaluate_context, source);
      TreePtr<InterfaceValue> interface_cast = term_unwrap_dyn_cast<InterfaceValue>(interface);
      if (!interface_cast) {
        SourceLocation interface_location(expression->location, source);
        evaluate_context->compile_context().error_throw(interface_location, "Interface description did not evaluate to an interface");
      }
      return interface_cast;
    }
    
    StatementMode statement_mode(StatementMode base_mode, const TreePtr<Term>& value, const SourceLocation& location) {
      switch (base_mode) {
      case statement_mode_destroy:
        return statement_mode_destroy;
      
      case statement_mode_value:
        if (value->is_type() && value->is_functional())
          return statement_mode_functional; // Make type declarations functional without the user writing '::'
        else if (term_unwrap_isa<FunctionType>(value->type))
          return statement_mode_ref; // If the value cannot be copied, automatically reference it
        else
          return statement_mode_value;
        
      case statement_mode_functional:
        if (!value->pure)
          value->compile_context().error_throw(location, "Cannot evaluate impure value to global functional constant.");
        return statement_mode_functional;
        
      case statement_mode_ref:
        switch (value->mode) {
        case result_mode_lvalue:
        case result_mode_rvalue:
          return statement_mode_ref;
          
        case result_mode_by_value: value->compile_context().error_throw(location, "Cannot create reference to temporary");
        case result_mode_functional: value->compile_context().error_throw(location, "Cannot create reference to functional value");
        default: PSI_FAIL("unexpected enum value");
        }
        
      default: PSI_FAIL("unexpected enum value");
      }
    }
    
    class BlockEntryCallback {
      SourceLocation m_location;
      SharedPtr<Parser::Expression> m_statement;
      StatementMode m_mode;
      bool m_is_result;
      
      static StatementMode block_statement_mode(StatementMode base_mode, bool is_result, const TreePtr<Term>& value, const SourceLocation& location) {
        if (is_result && (base_mode == statement_mode_destroy))
          base_mode = statement_mode_value;
        return statement_mode(base_mode, value, location);
      }
      
    public:
      BlockEntryCallback(const SourceLocation& location, const SharedPtr<Parser::Expression>& statement, StatementMode mode, bool is_result)
      : m_location(location), m_statement(statement), m_mode(mode), m_is_result(is_result) {
      }
      
      TreePtr<Statement> evaluate(const TreePtr<EvaluateContext>& context) {
        TreePtr<Term> value = compile_term(m_statement, context, m_location.logical);
        return TermBuilder::statement(value, block_statement_mode(m_mode, m_is_result, value, m_location), m_location);
      }
      
      template<typename V>
      static void visit(V& v) {
        v("location", &BlockEntryCallback::m_location)
        ("statement", &BlockEntryCallback::m_statement)
        ("mode", &BlockEntryCallback::m_mode)
        ("is_result", &BlockEntryCallback::m_is_result);
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
              logical_location = location.logical->new_child(expr_name);
            } else {
              logical_location = location.logical;
            }
            SourceLocation statement_location(named_expr.location, logical_location);
            m_statements.push_back(StatementValueType(compile_context(), statement_location,
                                                      BlockEntryCallback(statement_location, named_expr.expression, (StatementMode)named_expr.mode, boost::next(ii)==ie)));
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
          result.push_back(ii->get(*this, &BlockContext::get_ptr));
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
          return lookup_result_match(self.m_statements[it->second].get(self, &BlockContext::get_ptr));
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

      static void local_complete_impl(const BlockContext& self) {
        for (PSI_STD::vector<StatementValueType>::const_iterator ii = self.m_statements.begin(), ie = self.m_statements.end(); ii != ie; ++ii)
          ii->get(self, &BlockContext::get_ptr);
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
        result = TermBuilder::empty_value(evaluate_context->compile_context());
      return TermBuilder::block(block_statements, result, location);
    }

    /**
     * Utility function to compile contents of different bracket types as a sequence of statements.
     */
    TreePtr<Term> compile_from_bracket(const SharedPtr<Parser::TokenExpression>& expr,
                                       const TreePtr<EvaluateContext>& evaluate_context,
                                       const SourceLocation& location) {
      return compile_block(Parser::parse_statement_list(evaluate_context->compile_context().error_context(), location.logical, expr->text),
                           evaluate_context, location);
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
        TreePtr<Term> value = compile_term(m_statement, context, m_location.logical);
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
            LogicalSourceLocationPtr logical_location = location.logical->new_child(expr_name);
            SourceLocation entry_location(named_expr.location, logical_location);
            m_entries.insert(std::make_pair(expr_name, EntryType(compile_context(), entry_location,
                                                                 NamespaceEntry(named_expr.expression, (StatementMode)named_expr.mode, entry_location))));
          }
        }
      }
      
      Namespace::NameMapType names() const {
        Namespace::NameMapType result;
        for (NameMapType::const_iterator ii = m_entries.begin(), ie = m_entries.end(); ii != ie; ++ii)
          result.insert(std::make_pair(ii->first, ii->second.get(*this, &NamespaceContext::get_ptr)));
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
          return lookup_result_match(it->second.get(self, &NamespaceContext::get_ptr));
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

      static void local_complete_impl(const NamespaceContext& self) {
        for (NameMapType::const_iterator ii = self.m_entries.begin(), ie = self.m_entries.end(); ii != ie; ++ii)
          ii->second.get(self, &NamespaceContext::get_ptr);
      }
    };

    const EvaluateContextVtable NamespaceContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(NamespaceContext, "psi.compiler.NamespaceContext", EvaluateContext);

    TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) {
      TreePtr<NamespaceContext> nsc(::new NamespaceContext(statements, evaluate_context, location));
      return Namespace::new_(evaluate_context->compile_context(), nsc->names(), location);
    }
    
    struct ScriptEntryResult {
      /// \brief Value that should be bound to a the variable, if it is named
      TreePtr<Term> value;
      /// \brief Global variable generated by this entry
      TreePtr<Global> global;
      /// \brief Last global variable generated in the list
      TreePtr<Global> last;
      
      template<typename V>
      static void visit(V& v) {
        v("value", &ScriptEntryResult::value)
        ("global", &ScriptEntryResult::global)
        ("last", &ScriptEntryResult::last);
      }
    };
    
    typedef SharedDelayedValue<ScriptEntryResult, TreePtr<EvaluateContext> > ScriptEntryDelayed;
    
    class ScriptEntry {
      ScriptEntryDelayed m_previous;
      const CompileScriptCallback *m_callback;
      unsigned m_index;
      SharedPtr<Parser::Expression> m_statement;
      StatementMode m_mode;
      SourceLocation m_location;

    public:
      typedef Term TreeResultType;
      
      ScriptEntry(const ScriptEntryDelayed& previous,
                  const CompileScriptCallback *callback, unsigned index,
                  const SharedPtr<Parser::Expression>& expression, StatementMode mode, const SourceLocation& location)
      : m_previous(previous),
      m_callback(callback),
      m_index(index),
      m_statement(expression),
      m_mode(mode),
      m_location(location) {
      }
      
      template<typename Visitor>
      static void visit(Visitor& v) {
        v("index", &ScriptEntry::m_index)
        ("expression", &ScriptEntry::m_statement)
        ("mode", &ScriptEntry::m_mode)
        ("location", &ScriptEntry::m_location);
      }

      ScriptEntryResult evaluate(const TreePtr<EvaluateContext>& context) {
        TreePtr<Term> value = compile_term(m_statement, context, m_location.logical);
        StatementMode mode = statement_mode(m_mode, value, m_location);
        
        if ((mode == statement_mode_ref) && value->pure)
          mode = statement_mode_functional; // A pure reference should just be forwarded
        
        PSI_STD::vector<TreePtr<Statement> > block_entries;
        TreePtr<Term> user_value;
        if (mode == statement_mode_functional) {
          user_value = value;
        } else {
          TreePtr<Statement> value_stmt = TermBuilder::statement(value, mode, m_location);
          block_entries.push_back(value_stmt);
          user_value = value_stmt;
        }
        
        if (TreePtr<Term> callback_term = m_callback->run(m_index, user_value, m_location))
          block_entries.push_back(TermBuilder::statement(callback_term, statement_mode_destroy, m_location));
        
        ScriptEntryResult result;
        if (!block_entries.empty()) {
          TreePtr<Term> block_result;
          
          // Create artificial dependency to previous entry
          // so that it should be initialized first
          if (!m_previous.empty()) {
            if (TreePtr<Term> prev_global = m_previous.get(context).last)
              block_entries.insert(block_entries.begin(), TermBuilder::statement(prev_global, statement_mode_ref, m_location));
          }

          if (mode == statement_mode_value)
            block_result = user_value;
          else if (mode == statement_mode_ref)
            block_result = TermBuilder::ptr_to(user_value, m_location);
          else
            block_result = TermBuilder::empty_value(context->compile_context());

          TreePtr<Term> block = TermBuilder::block(block_entries, block_result, m_location);
          
          result.last = result.global = TermBuilder::global_variable(context->module(), link_public, false, false, m_location, block);
          if (mode == statement_mode_value)
            result.value = result.global;
          else if (mode == statement_mode_ref)
            result.value = TermBuilder::ptr_target(result.global, m_location);
          else if (mode == statement_mode_functional)
            result.value = value;
        } else {
          PSI_ASSERT(mode == statement_mode_functional);
          result.value = value;
          if (!m_previous.empty())
            result.last = m_previous.get(context).last;
        }
        
        return result;
      }
    };
    
    class ScriptContext : public EvaluateContext {
      typedef PSI_STD::vector<ScriptEntryDelayed> EntryList;
      typedef PSI_STD::map<String, std::size_t> NameMapType;
      EntryList m_entries;
      NameMapType m_named_entries;
      TreePtr<EvaluateContext> m_next;
      
      TreePtr<EvaluateContext> get_ptr() const {return tree_from(this);}

    public:
      static const EvaluateContextVtable vtable;

      ScriptContext(const CompileScriptCallback *callback,
                    const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                    const TreePtr<EvaluateContext>& next,
                    const SourceLocation& location)
      : EvaluateContext(&vtable, next->module(), location),
      m_next(next) {
        unsigned index = 0;
        ScriptEntryDelayed previous;
        for (PSI_STD::vector<SharedPtr<Parser::Statement> >::const_iterator ii = statements.begin(), ie = statements.end(); ii != ie; ++ii, ++index) {
          if (*ii) {
            const Parser::Statement& named_expr = **ii;
            LogicalSourceLocationPtr logical_location = location.logical;
            if (named_expr.name) {
              PSI_ASSERT(named_expr.mode != statement_mode_destroy);
              String s = named_expr.name->str();
              logical_location = location.logical->new_child(s);
              m_named_entries.insert(std::make_pair(s, m_entries.size()));
            }
            SourceLocation entry_location(named_expr.location, logical_location);
            
            ScriptEntryDelayed entry(compile_context(), entry_location,
                                     ScriptEntry(previous, callback, index, named_expr.expression, (StatementMode)named_expr.mode, entry_location));
            m_entries.push_back(entry);
            previous = entry;
          }
        }
      }
      
      PSI_STD::vector<TreePtr<Global> > entries() const {
        local_complete_impl(*this);
        
        PSI_STD::vector<TreePtr<Global> > result;
        for (EntryList::const_iterator ii = m_entries.begin(), ie = m_entries.end(); ii != ie; ++ii) {
          const TreePtr<Global>& entry = ii->get_checked().global;
          if (entry)
            result.push_back(entry);
        }
        return result;
      }
      
      PSI_STD::map<String, TreePtr<Term> > names() const {
        local_complete_impl(*this);
        
        PSI_STD::map<String, TreePtr<Term> > result;
        for (NameMapType::const_iterator ii = m_named_entries.begin(), ie = m_named_entries.end(); ii != ie; ++ii)
          result.insert(std::make_pair(ii->first, m_entries[ii->second].get_checked().value));
        return result;
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("entries", &ScriptContext::m_entries)
        ("next", &ScriptContext::m_next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const ScriptContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        NameMapType::const_iterator it = self.m_named_entries.find(name);
        if (it != self.m_named_entries.end()) {
          return lookup_result_match(self.m_entries[it->second].get(self, &ScriptContext::get_ptr).value);
        } else if (self.m_next) {
          return self.m_next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
      
      static void overload_list_impl(const ScriptContext& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.m_next)
          self.m_next->overload_list(overload_type, overload_list);
      }

      static void local_complete_impl(const ScriptContext& self) {
        for (EntryList::const_iterator ii = self.m_entries.begin(), ie = self.m_entries.end(); ii != ie; ++ii)
          ii->get(self, &ScriptContext::get_ptr);
      }
    };
    
    const EvaluateContextVtable ScriptContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(ScriptContext, "psi.compiler.ScriptContext", EvaluateContext);

    /**
     * \brief Compile a list of statements as a script.
     * 
     * This turns named variables into globals.
     * 
     * \return Expressions for statements which were named.
     */
    CompileScriptResult compile_script(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                                       const TreePtr<EvaluateContext>& evaluate_context, const CompileScriptCallback& callback,
                                       const SourceLocation& location) {
      TreePtr<ScriptContext> nsc(::new ScriptContext(&callback, statements, evaluate_context, location));
      CompileScriptResult result;
      result.globals = nsc->entries();
      result.names = nsc->names();
      return result;
    }
  }
}
