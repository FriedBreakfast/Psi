#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/next_prior.hpp>

#include "Compiler.hpp"
#include "Tree.hpp"
#include "TreePattern.hpp"
#include "Platform.hpp"
#include "Parser.hpp"

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
    
    class EvaluateContextDictionary : public CompileImplementation {
      virtual void gc_visit(GCVisitor& visitor) {
        for (NameMapType::iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
          visitor.visit_ptr(ii->second);
      }

      struct Callback {
        LookupResult<TreePtr<> > lookup(const TreePtr<EvaluateContextDictionary>& data, const String& name) {
          NameMapType::const_iterator it = data->entries.find(name);
          if (it != data->entries.end()) {
            return lookup_result_match(it->second);
          } else if (data->next) {
            return compile_implementation_wrap<EvaluateContextRef>(data->next).lookup(name);
          } else {
            return lookup_result_none;
          }
        }
      };

      static EvaluateContextWrapper<Callback, EvaluateContextDictionary> m_vtable;

    public:
      typedef std::map<String, TreePtr<> > NameMapType;
      NameMapType entries;
      TreePtr<CompileImplementation> next;

      EvaluateContextDictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<> >&, const TreePtr<CompileImplementation>&);
    };
    
    EvaluateContextWrapper<EvaluateContextDictionary::Callback, EvaluateContextDictionary> EvaluateContextDictionary::m_vtable;

    EvaluateContextDictionary::EvaluateContextDictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<> >& entries_, const TreePtr<CompileImplementation>& next_)
    : CompileImplementation(compile_context, location), entries(entries_), next(next_) {
      vtable = compile_context.tree_from_address(location, TreePtr<Type>(), &m_vtable);
    }

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<CompileImplementation> evaluate_context_dictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<> >& entries, const TreePtr<CompileImplementation>& next) {
      return TreePtr<CompileImplementation>(new EvaluateContextDictionary(compile_context, location, entries, next));
    }

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<CompileImplementation> evaluate_context_dictionary(CompileContext& compile_context, const SourceLocation& location, const std::map<String, TreePtr<> >& entries) {
      return evaluate_context_dictionary(compile_context, location, entries, TreePtr<CompileImplementation>());
    }

    /**
     * \brief OutputStreamable class which can be used to turn an expression into a simple string.
     */
    class ExpressionString {
      PhysicalSourceLocation m_location;

    public:
      ExpressionString(const SharedPtr<Parser::Expression>& expr) : m_location(expr->location) {
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
     * \param evaluate_context Context in which to lookup names.
     * \param source Logical (i.e. namespace etc.) location of the expression, for symbol naming and debugging.
     */
    TreePtr<> compile_expression(const SharedPtr<Parser::Expression>& expression,
                                 const TreePtr<CompileImplementation>& evaluate_context,
                                 const SharedPtr<LogicalSourceLocation>& source) {

      CompileContext& compile_context = evaluate_context->compile_context();
      SourceLocation location(expression->location, source);

      switch (expression->expression_type) {
      case Parser::expression_macro: {
        const Parser::MacroExpression& macro_expression = checked_cast<Parser::MacroExpression&>(*expression);

        TreePtr<> first = compile_expression(macro_expression.elements.front(), evaluate_context, source);
        ArrayList<SharedPtr<Parser::Expression> > rest(boost::next(macro_expression.elements.begin()), macro_expression.elements.end());

        if (!first->type())
          compile_context.error_throw(location, "Term does not have a type", CompileContext::error_internal);
        MacroRef first_macro = compile_implementation_lookup<MacroRef>(compile_context.macro_interface(), first->type(), location);
        if (!first_macro)
          compile_context.error_throw(location, "Type does not have an associated macro", CompileContext::error_internal);

        return first_macro.evaluate(first, rest, evaluate_context, location);
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

          LookupResult<TreePtr<> > first = compile_implementation_wrap<EvaluateContextRef>(evaluate_context, location).lookup(bracket_operation);
          switch (first.type()) {
          case lookup_result_type_none:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator missing") % bracket_str % bracket_operation);
          case lookup_result_type_conflict:
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator lookup ambiguous") % bracket_str % bracket_operation);
          default: break;
          }

          if (!first.value())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: successful lookup of '%s' returned NULL value") % bracket_str % bracket_operation, CompileContext::error_internal);

          if (!first.value()->type())
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator does not have a type") % bracket_str % bracket_operation, CompileContext::error_internal);
          MacroRef first_macro = compile_implementation_lookup<MacroRef>(compile_context.macro_interface(), first.value()->type(), location);
          if (!first_macro)
            compile_context.error_throw(location, boost::format("Cannot evaluate %s bracket: '%s' operator's type does not have an associated macro") % bracket_str % bracket_operation, CompileContext::error_internal);

          ArrayList<SharedPtr<Parser::Expression> > expression_list(1, expression);
          return first_macro.evaluate(first.value(), expression_list, evaluate_context, location);
        }

        case Parser::TokenExpression::identifier: {
          String name(token_expression.text.begin, token_expression.text.end);
          LookupResult<TreePtr<> > result = compile_implementation_wrap<EvaluateContextRef>(evaluate_context, location).lookup(name);

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

        TreePtr<> left = compile_expression(dot_expression.left, evaluate_context, source);
        if (!left->type())
          compile_context.error_throw(location, "Term does not have a type", CompileContext::error_internal);
        MacroRef left_macro = compile_implementation_lookup<MacroRef>(compile_context.macro_interface(), left->type(), location);
        if (!left_macro)
          compile_context.error_throw(location, "Type does not have an associated macro", CompileContext::error_internal);

        return left_macro.dot(left, dot_expression.right, evaluate_context, location);
      }

      default:
        PSI_FAIL("unknown expression type");
      }
    }

    class StatementListEntry : public Tree {
      virtual void gc_visit(GCVisitor& visitor) {
        visitor % statement;
      }

    public:
      StatementListEntry(CompileContext& compile_context, const SourceLocation& location, typename MoveRef<DependencyPtr>::type dependency)
      : Tree(compile_context, location, move_ref(dependency)) {}

      TreePtr<Statement> statement;
    };

    class StatementCompiler : public DependencyBase<StatementCompiler, StatementListEntry> {
      SharedPtr<Parser::Expression> m_expression;
      SharedPtr<LogicalSourceLocation> m_logical_location;
      TreePtr<CompileImplementation> m_evaluate_context;

    public:
      StatementCompiler(const SharedPtr<Parser::Expression>& expression, const SharedPtr<LogicalSourceLocation>& logical_location, const TreePtr<CompileImplementation>& evaluate_context)
      : m_expression(expression), m_logical_location(logical_location), m_evaluate_context(evaluate_context) {
      }
      
      void gc_visit(GCVisitor& visitor) {
        visitor % m_evaluate_context;
      }

      void run(const TreePtr<StatementListEntry>& entry) {
        TreePtr<> expr = compile_expression(m_expression, m_evaluate_context, m_logical_location);
        entry->statement.reset(new Statement(expr));
      }
    };

    class StatementListCompiler : public DependencyBase<StatementListCompiler, Block> {
      std::vector<TreePtr<StatementListEntry> > m_statements;

    public:
      StatementListCompiler(const std::vector<TreePtr<StatementListEntry> >& statements)
      : m_statements(statements) {
      }

      void gc_visit(GCVisitor& visitor) {
        for (std::vector<TreePtr<StatementListEntry> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii)
          visitor % *ii;
      }

      void run(const TreePtr<Block>& block) {
        for (std::vector<TreePtr<StatementListEntry> >::iterator ii = m_statements.begin(), ie = m_statements.end(); ii != ie; ++ii) {
          (*ii)->complete();
          block->statements.push_back((*ii)->statement);
        }
      }
    };

    class StatementListContext : public CompileImplementation {
      virtual void gc_visit(GCVisitor& visitor) {
        Tree::gc_visit(visitor);
        visitor % next;
        for (NameMapType::iterator ii = entries.begin(), ie = entries.end(); ii != ie; ++ii)
          visitor % ii->second;
      }

      struct Callback {
        LookupResult<TreePtr<> > lookup(const TreePtr<StatementListContext>& data, const String& name) {
          StatementListContext::NameMapType::const_iterator it = data->entries.find(name);
          if (it != data->entries.end()) {
            it->second->complete();
            return lookup_result_match(it->second->statement);
          } else if (data->next) {
            return compile_implementation_wrap<EvaluateContextRef>(data->next).lookup(name);
          } else {
            return lookup_result_none;
          }
        }
      };

      static EvaluateContextWrapper<Callback, StatementListContext> m_vtable;

    public:
      typedef std::map<String, TreePtr<StatementListEntry> > NameMapType;
      NameMapType entries;
      TreePtr<CompileImplementation> next;

      StatementListContext(CompileContext& compile_context,
                           const SourceLocation& location,
                           const TreePtr<CompileImplementation>& next_)
      : CompileImplementation(compile_context, location), next(next_) {
      }
    };

    EvaluateContextWrapper<StatementListContext::Callback, StatementListContext> StatementListContext::m_vtable;

    SharedPtr<LogicalSourceLocation> make_logical_location(const SharedPtr<LogicalSourceLocation>& parent, const String& name) {
      SharedPtr<LogicalSourceLocation> result(new LogicalSourceLocation);
      result->parent = parent;
      result->name = name;
      return result;
    }

    TreePtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements,
                                          const TreePtr<CompileImplementation>& evaluate_context,
                                          const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context->compile_context();
      TreePtr<StatementListContext> context_tree(new StatementListContext(compile_context, location, evaluate_context));
      std::vector<TreePtr<StatementListEntry> > compiler_trees;

      bool use_last = false;
      for (std::vector<boost::shared_ptr<Parser::NamedExpression> >::const_iterator ii = statements.begin(), ib = statements.begin(), ie = statements.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        if ((use_last = named_expr.expression.get())) {
          String expr_name = named_expr.name ? String(named_expr.name->begin, named_expr.name->end) : String();
          SourceLocation statement_location(named_expr.location, make_logical_location(location.logical, expr_name));
          DependencyPtr statement_compiler(new StatementCompiler(named_expr.expression, statement_location.logical, context_tree));
          TreePtr<StatementListEntry> statement_tree(new StatementListEntry(compile_context, statement_location, move_ref(statement_compiler)));
          compiler_trees.push_back(statement_tree);
          
          if (named_expr.name)
            context_tree->entries[expr_name] = statement_tree;
        }
      }

      TreePtr<Type> block_type;
      if (use_last) {
      } else {
        PSI_FAIL("not implemented");
      }

      DependencyPtr list_compiler(new StatementListCompiler(compiler_trees));
      TreePtr<Block> block(new Block(block_type, location, list_compiler));

      return block;
    }
  }
}
