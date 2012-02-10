#include "Compiler.hpp"
#include "Tree.hpp"
#include "Parser.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    /**
     * Class member information required by both ClassInfo and ClassMemberInfo.
     */
    struct ClassMemberInfoCommon {
      /// Static data value.
      TreePtr<Term> static_value;
      /// Member data type.
      TreePtr<Term> member_type;
      /// Callback to be used when this member is accessed statically.
      TreePtr<MacroDotCallback> static_callback;
      /// Callback to be used when this member is accessed on an object.
      TreePtr<MacroDotCallback> member_callback;
    };
    
    /**
     * Data supplied by class members.
     */
    struct ClassMemberInfo : ClassMemberInfoCommon {
      /// Implementations
      PSI_STD::vector<TreePtr<Implementation> > implementations;
    };

    struct ClassMemberNamed : ClassMemberInfoCommon {
      String name;
    };

    struct ClassInfo {
      /// Collection of all implementations in this class (since implementations do not have names)
      PSI_STD::vector<TreePtr<Implementation> > implementations;
      /// List of members, which may or may not be named, however it is an error if non-empty names are not unique.
      PSI_STD::vector<ClassMemberNamed> members;
    };

    class ClassMemberInfoCallback;
    
    struct ClassMemberInfoCallbackVtable {
      TreeVtable base;
      void (*class_member_info) (ClassMemberInfo*, const ClassMemberInfoCallback*);
    };

    /**
     * Tree type used to get class member information.
     */
    class ClassMemberInfoCallback : public Tree {
    public:
      typedef ClassMemberInfoCallbackVtable VtableType;
      static const SIVtable vtable;

      ClassMemberInfo class_member_info() const {
        ResultStorage<ClassMemberInfo> result;
        derived_vptr(this)->class_member_info(result.ptr(), this);
        return result.done();
      }
    };

    const SIVtable ClassMemberInfoCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ClassMemberInfoCallback", Tree);

    class ClassMutator;

    struct ClassMutatorVtable {
      TreeVtable base;
      void (*before) (const ClassMutator*, ClassInfo*);
      void (*after) (const ClassMutator*, ClassInfo*);
    };

    /**
     * Tree type which supports class mutator callbacks.
     */
    class ClassMutator : public Tree {
    public:
      typedef ClassMutatorVtable VtableType;
      static const SIVtable vtable;

      /**
       * Called before class member processing.
       */
      void before(ClassInfo& class_info) const {
        derived_vptr(this)->before(this, &class_info);
      }

      /**
       * Called after class member processing.
       */
      void after(ClassInfo& class_info) const {
        derived_vptr(this)->after(this, &class_info);
      }
    };

    const SIVtable ClassMutator::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ClassMutator", Tree);

    class ClassCompilerTree : public Tree {
    public:
      static const TreeVtable vtable;

      typedef std::map<String, TreePtr<Term> > NameMapType;

      ClassCompilerTree(const TreePtr<Block>& block_, const NameMapType& entries_)
      : Tree(block_.compile_context(), block_.location()),
      block(block_),
      entries(entries_) {
        PSI_COMPILER_TREE_INIT();
      }

      TreePtr<Block> block;
      NameMapType entries;

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("block", &ClassCompilerTree::block)
        ("entries", &ClassCompilerTree::entries);
      }
    };
    
    const TreeVtable ClassCompilerTree::vtable = PSI_COMPILER_TREE(ClassCompilerTree, "psi.compiler.ClassCompilerTree", Tree);

    class ClassCompilerContext : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef ClassCompilerTree::NameMapType NameMapType;

      ClassCompilerContext(const TreePtr<StatementListTree>& class_compiler_,
                           const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(class_compiler_.compile_context(), class_compiler_.location()),
      class_compiler(class_compiler_),
      next(next_) {
        PSI_COMPILER_TREE_INIT();
      }

      TreePtr<ClassCompilerTree> class_compiler;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("class_compiler", &ClassCompilerContext::class_compiler)
        ("next", &ClassCompilerContext::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const ClassCompilerContext& self, const String& name) {
        ClassCompilerContext::NameMapType::const_iterator it = self.class_compiler->entries.find(name);
        if (it != self.class_compiler->entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable ClassCompilerContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(ClassCompilerContext, "psi.compiler.ClassCompilerContext", EvaluateContext);

    class ClassMemberCompiler {
      TreePtr<EvaluateContext> m_context;
      SharedPtr<Parser::TokenExpression> m_expression;

    public:
      ClassMemberCompiler(const TreePtr<EvaluateContext>& context,
                          const SharedPtr<Parser::TokenExpression>& expression)
      : m_context(context), m_expression(expression) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("context", &ClassMemberCompiler::m_context)
        ("expression", &ClassMemberCompiler::m_expression);
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& self) {
        return compile_expression(m_expression, m_context, self.location());
      }
    };

    class ClassCompiler {
      TreePtr<EvaluateContext> m_context;
      SharedPtr<Parser::TokenExpression> m_mutators;
      SharedPtr<Parser::TokenExpression> m_members;

    public:
      FunctionBodyCompiler(const TreePtr<EvaluateContext>& context,
                           const SharedPtr<Parser::TokenExpression>& mutators,
                           const SharedPtr<Parser::TokenExpression>& members)
      : m_context(context), m_mutators(mutators), m_members(members) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("context", &ClassCompiler::m_context)
        ("mutators", &ClassCompiler::m_mutators)
        ("members", &ClassCompiler::m_members);
      }

      TreePtr<ClassCompilerTree> evaluate(const TreePtr<Term>& self) {
        CompileContext& compile_context = self.compile_context();

        PSI_STD::vector<SharedPtr<Parser::Expression> > mutator_expressions;
        if (m_mutators)
          mutator_expressions = Parser::parse_positional_list(m_mutators->text);
        PSI_STD::vector<SharedPtr<Parser::NamedExpression> > member_expressions = Parser::parse_statement_list(m_members->text);

        PSI_STD::vector<TreePtr<ClassMutator> > mutator_trees;

        ClassInfo info;

        // Run pre-mutation
        for (PSI_STD::vector<TreePtr<ClassMutator> >::iterator ii = mutator_trees.begin(), ie = mutator_trees.end(); ii != ie; ++ii)
          (*ii)->before(info);

        TreePtr<EvaluateContext> member_context;

        // Get member trees
        PSI_STD::vector<TreePtr<ClassMemberInfoCallback> > entries;
        PSI_STD::map<String, TreePtr<ClassMemberInfoCallback> > named_entries;
        for (PSI_STD::vector<SharedPtr<Parser::NamedExpression> >::iterator ii = member_expressions.begin(), ie = member_expressions.end(); ii != ie; ++ii) {
          const Parser::NamedExpression& named_expr = **ii;

          String expr_name;
          LogicalSourceLocationPtr logical_location;
          if (named_expr.name) {
            expr_name = String(named_expr.name->begin, named_expr.name->end);
            logical_location = self.location().logical->named_child(expr_name);
          } else {
            logical_location = self.location().logical->new_anonymous_child();
          }
          SourceLocation member_location(named_expr.location.location, logical_location);
          TreePtr<ClassMemberInfoCallback> entry = tree_callback<ClassMemberInfoCallback>(compile_context, member_location, ClassMemberCompiler(named_expr.expression, member_context));
          entries.push_back(entry);

          if (named_expr.name)
            named_entries[expr_name] = entry;
        }

        // Run post-mutation
        for (PSI_STD::vector<TreePtr<ClassMutator> >::reverse_iterator ii = mutator_trees.rbegin(), ie = mutator_trees.rend(); ii != ie; ++ii)
          (*ii)->after(info);
      }
    };

    /**
     * Compile a function definition, and return a macro for invoking it.
     */
    TreePtr<Term> compile_class_definition(const List<SharedPtr<Parser::Expression> >& arguments,
                                           const TreePtr<EvaluateContext>& evaluate_context,
                                           const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context.compile_context();

      if ((arguments.size() != 1) && (arguments.size() != 2))
        compile_context.error_throw(location, boost::format("class macro expects one or two arguments, got %s") % arguments.size());

      SharedPtr<Parser::TokenExpression> mutators, members;
      if (arguments.size() == 2)
        if (!(mutators = Parser::expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
          compile_context.error_throw(location, "Mutator argument to class definition is not a (...)");

      if (!(members = Parser::expression_as_token_type(arguments[arguments.size()-1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Members argument to class definition is not a [...]");

      return tree_callback<ClassCompilerTree>(compile_context, location, ClassCompiler(evaluate_context, mutators, members));
    }

    class ClassDefineCallback : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      ClassDefineCallback(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(compile_context, location) {
        PSI_COMPILER_TREE_INIT();
      }

      static TreePtr<Term> evaluate_impl(const ClassDefineCallback&,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        return compile_class_definition(arguments, evaluate_context, location);
      }
    };

    const MacroEvaluateCallbackVtable ClassDefineCallback::vtable =
    PSI_COMPILER_MACRO_EVALUATE_CALLBACK(ClassDefineCallback, "psi.compiler.ClassDefineCallback", MacroEvaluateCallback);

    /**
     * \brief Create a callback to the function definition function.
     */
    TreePtr<Term> class_definition_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroEvaluateCallback> callback(new ClassDefineCallback(compile_context, location));
      TreePtr<Macro> macro = make_macro(compile_context, location, callback);
      return make_macro_term(compile_context, location, macro);
    }
  }
}
