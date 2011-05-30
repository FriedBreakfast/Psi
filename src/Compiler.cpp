#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

#include "Compiler.hpp"
#include "Tree.hpp"
#include "Platform.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Compiler {
    bool si_is_a(SIBase *object, const SIVtable *cls) {
      for (const SIVtable *super = object->m_vptr; super; super = super->super) {
        if (super == cls)
          return true;
      }

      return false;
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
      
      *m_error_stream << boost::format("%s:%s: in '%s'\n") % loc.physical.file->url % loc.physical.first_line % logical_location_name(loc.logical);
      *m_error_stream << boost::format("%s:%s: %s:%s\n") % loc.physical.file->url % loc.physical.first_line % type % message;
    }

    void CompileContext::error_throw(const SourceLocation& loc, const std::string& message, unsigned flags) {
      error(loc, message, flags);
      throw CompileException();
    }

    /**
     * \brief JIT compile a global symbol.
     */
    void* CompileContext::jit_compile(const TreePtr<GlobalTree>& global) {
      if (!global->m_jit_ptr) {
        PSI_FAIL("not implemented");
      }

      return global->m_jit_ptr;
    }

    /**
     * \brief Create a tree for a global from the address of that global.
     */
    TreePtr<GlobalTree> CompileContext::tree_from_address(const SourceLocation& location, const TreePtr<Type>& type, void *ptr) {
      void *base;
      String name;
      try {
        name = Platform::address_to_symbol(ptr, &base);
      } catch (Platform::PlatformError& e) {
        error_throw(location, boost::format("Internal error: failed to get symbol name from addres: %s") % e.what());
      }
      if (base != ptr)
        error_throw(location, "Internal error: address used to retrieve symbol did not match symbol base");

      TreePtr<ExternalGlobalTree> result(new ExternalGlobalTree(type, location));
      result->symbol_name = name;
      result->m_jit_ptr = base;
      return result;
    }
    
    class EvaluateContextDictionary : public EvaluateContext {
      typedef std::map<String, TreePtr<Expression> > NameMapType;
      NameMapType m_entries;
      TreePtr<EvaluateContext> m_next;

    public:
      static const EvaluateContextVtable vtable;

      EvaluateContextDictionary(CompileContext& compile_context,
                                const SourceLocation& location,
                                const std::map<String, TreePtr<Expression> >& entries,
                                const TreePtr<EvaluateContext>& next)
      : EvaluateContext(compile_context, location), m_entries(entries), m_next(next) {
        m_vptr = reinterpret_cast<const SIVtable*>(&vtable);
      }

      template<typename Visitor>
      static void visit_impl(EvaluateContextDictionary& self, Visitor& visitor) {
        PSI_FAIL("not implemented");
        visitor
        ("entries", self.m_entries)
        ("next", self.m_next);
      }

      static LookupResult<TreePtr<Expression> > lookup_impl(EvaluateContextDictionary& self, const String& name) {
        NameMapType::const_iterator it = self.m_entries.find(name);
        if (it != self.m_entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.m_next) {
          return self.m_next->lookup(name);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable EvaluateContextDictionary::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextDictionary, "psi.compiler.EvaluateContextDictionary", &EvaluateContext::vtable);

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<Expression> >& entries, const TreePtr<EvaluateContext>& next) {
      return TreePtr<EvaluateContext>(new EvaluateContextDictionary(compile_context, location, entries, next));
    }

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<Expression> >& entries) {
      return evaluate_context_dictionary(compile_context, location, entries, TreePtr<EvaluateContext>());
    }

    /**
     * \brief OutputStreamable class which can be used to turn an expression into a simple string.
     */
    class ExpressionString {
      const char *m_begin;
      std::size_t m_count;

    public:
      ExpressionString(SharedPtr<Parser::Expression>& expr) {
        m_begin = expr->location.begin;
        m_count = expr->location.end - expr->location.begin;
      }
      
      friend std::ostream& operator << (std::ostream& os, const ExpressionString& self) {
        os.write(self.m_begin, self.m_count);
        return os;
      }
    };

    TreePtr<Macro> expression_macro(const TreePtr<Expression>& expr, const SourceLocation& location) {
      CompileContext& compile_context = expr->compile_context();

      if (!expr->meta())
        compile_context.error_throw(location, "Expression does not have a metatype", CompileContext::error_internal);

      TreePtr<> first_macro = interface_lookup(compile_context.macro_interface(), expr->meta(), location);
      if (!first_macro)
        compile_context.error_throw(location, "Type does not have an associated macro", CompileContext::error_internal);

      TreePtr<Macro> cast_first_macro = dyn_treeptr_cast<Macro>(first_macro);
      if (!cast_first_macro)
        compile_context.error_throw(location, "Interface value is of the wrong type", CompileContext::error_internal);

      return cast_first_macro;
    }
    
    /**
     * \brief Compile an expression.
     *
     * \param expression Expression, usually as produced by the parser.
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     */
    TreePtr<Expression> compile_expression(const SharedPtr<Parser::Expression>& expression,
                                           const TreePtr<EvaluateContext>& evaluate_context,
                                           const SharedPtr<LogicalSourceLocation>& source) {

      CompileContext& compile_context = evaluate_context->compile_context();
      SourceLocation location(expression->location.location, source);

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        TreePtr<Expression> first = compile_expression(macro_expression.elements.front(), evaluate_context, source);
        PSI_STD::vector<SharedPtr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());

        return expression_macro(first, location)->evaluate(first, rest, evaluate_context, location);
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

          LookupResult<TreePtr<Expression> > first = evaluate_context->lookup(bracket_operation);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator missing") % bracket_str % bracket_operation);
          case lookup_result_type_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator lookup ambiguous") % bracket_str % bracket_operation);
          default: break;
          }

          if (!first.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successful lookup of '%s' returned NULL value") % bracket_str % bracket_operation, CompileContext::error_internal);

          PSI_STD::vector<SharedPtr<Parser::Expression> > expression_list(1, expression);
          return expression_macro(first.value(), location)->evaluate(first.value(), expression_list, evaluate_context, location);
        }

        case Parser::TokenExpression::identifier: {
          String name(token_expression.text.begin, token_expression.text.end);
          LookupResult<TreePtr<Expression> > result = evaluate_context->lookup(name);

          switch (result.type()) {
          case lookup_result_type_none: compile_context.error_throw(location, boost::format("Name not found: %s") % name);
          case lookup_result_type_conflict: compile_context.error_throw(location, boost::format("Conflict on lookup of: %s") % name);
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
        TreePtr<Expression> left = compile_expression(dot_expression.left, evaluate_context, source);
        return expression_macro(left, location)->dot(left, dot_expression.right, evaluate_context, location);
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class StatementListEntry : public Tree {
    public:
      StatementListEntry(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {}

      TreePtr<Statement> statement;

      template<typename Visitor>
      static void visit_impl(StatementListEntry& self, Visitor& visitor) {
      }
    };

    class StatementListCompiler {
      std::vector<TreePtr<StatementListEntry> > m_statements;

    public:
      StatementListCompiler(const std::vector<TreePtr<StatementListEntry> >& statements)
      : m_statements(statements) {
      }

      template<typename Visitor>
      static void visit_impl(StatementListCompiler& self, Visitor& visitor) {
        for (std::vector<TreePtr<StatementListEntry> >::iterator ii = self.m_statements.begin(), ie = self.m_statements.end(); ii != ie; ++ii)
          visitor("", *ii);
      }

      void run(const TreePtr<Block>& block) {
        for (std::vector<TreePtr<StatementListEntry> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii) {
          (*ii)->complete();
          block->statements.push_back((*ii)->statement);
        }
      }
    };

    SharedPtr<LogicalSourceLocation> make_logical_location(const SharedPtr<LogicalSourceLocation>& parent, const String& name) {
      SharedPtr<LogicalSourceLocation> result(new LogicalSourceLocation);
      result->parent = parent;
      result->name = name;
      return result;
    }

    String logical_location_name(const SharedPtr<LogicalSourceLocation>& location) {
      std::stringstream ss;
      if (!location->name.empty())
        ss << location->name;
      else
        ss << "(anonymous)";
      
      for (LogicalSourceLocation *l = location->parent.get(); l; l = location->parent.get()) {
        ss << '.';
        if (!l->name.empty())
          ss << l->name;
        else
          ss << "(anonymous)";
      }
      const std::string& sss = ss.str();
      return String(sss.c_str(), sss.length());
    }

    class StatementListStatement : public Statement {
      struct StatementData {
        SharedPtr<Parser::Expression> expression;
        SharedPtr<LogicalSourceLocation> logical_location;
        TreePtr<EvaluateContext> evaluate_context;
      };

      UniquePtr<StatementData> m_data;

    public:
      StatementListStatement(CompileContext& compile_context, const SourceLocation& location,
        const SharedPtr<Parser::Expression>& expression,
        const TreePtr<EvaluateContext>& evaluate_context) 
      : Statement(compile_context, location) {

        m_data.reset(new StatementData);
        m_data->expression = expression;
        m_data->logical_location = location.logical;
        m_data->evaluate_context = evaluate_context;
      }
      
      template<typename Visitor>
      static void visit_impl(StatementListStatement& self, Visitor& visitor) {
        if (self.m_data)
          visitor("evaluate_context", self.m_data->evaluate_context);
      }

      static void complete_callback_impl(StatementListStatement& self) {
        self.complete_statement();
      }

      static void complete_statement_impl(StatementListStatement& self) {
        UniquePtr<StatementData> data;
        data.swap(self.m_data);
        self.value = compile_expression(data->expression, data->evaluate_context, data->logical_location);
      }
    };
    
    class StatementListContext : public EvaluateContext {
    public:
      typedef std::map<String, TreePtr<StatementListStatement> > NameMapType;
      NameMapType entries;

    private:
      TreePtr<EvaluateContext> m_next;

    public:
      StatementListContext(CompileContext& compile_context,
                           const SourceLocation& location,
                           const TreePtr<EvaluateContext>& next)
      : EvaluateContext(compile_context, location), m_next(next) {
      }

      template<typename Visitor>
      static void visit_impl(StatementListContext& self, Visitor& visitor) {
        EvaluateContext::visit_impl(self, visitor);
        visitor("next", self.m_next);
        for (NameMapType::iterator ii = self.entries.begin(), ie = self.entries.end(); ii != ie; ++ii)
          visitor("", ii->second);
      }

      static LookupResult<TreePtr<Expression> > lookup_impl(const StatementListContext& self, const String& name) {
        StatementListContext::NameMapType::const_iterator it = self.entries.find(name);
        if (it != self.entries.end()) {
          it->second->complete_statement();
          return lookup_result_match(it->second);
        } else if (self.m_next) {
          return self.m_next->lookup(name);
        } else {
          return lookup_result_none;
        }
      }
    };

    TreePtr<Block> compile_statement_list(List<SharedPtr<Parser::NamedExpression> >& statements,
                                          const TreePtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context->compile_context();
      TreePtr<StatementListContext> context_tree(new StatementListContext(compile_context, location, evaluate_context));
      PSI_STD::vector<TreePtr<Statement> > statement_trees;

      bool use_last = false;
      for (LocalIterator<SharedPtr<Parser::NamedExpression> > ii(statements); ii.next();) {
        const Parser::NamedExpression& named_expr = *ii.current();
        if ((use_last = named_expr.expression.get())) {
          String expr_name = named_expr.name ? String(named_expr.name->begin, named_expr.name->end) : String();
          SourceLocation statement_location(named_expr.location.location, make_logical_location(location.logical, expr_name));
          TreePtr<StatementListStatement> statement_tree(new StatementListStatement(compile_context, statement_location, named_expr.expression, context_tree));
          statement_trees.push_back(statement_tree);
          
          if (named_expr.name)
            context_tree->entries[expr_name] = statement_tree;
        }
      }

      TreePtr<Type> block_type;
      if (use_last) {
      } else {
        PSI_FAIL("not implemented");
      }

      TreePtr<Block> block(new Block(block_type, location));
      block->statements.swap(statement_trees);

      return block;
    }
  }
}
