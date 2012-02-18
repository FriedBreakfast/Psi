#include "Compiler.hpp"
#include "Tree.hpp"
#include "Parser.hpp"
#include "Macros.hpp"

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

      template<typename V>
      static void visit(V& v) {
        v("static_value", &ClassMemberInfoCommon::static_value)
        ("member_type", &ClassMemberInfoCommon::member_type)
        ("static_callback", &ClassMemberInfoCommon::static_callback)
        ("member_callback", &ClassMemberInfoCommon::member_callback);
      }
    };
    
    /**
     * Data supplied by class members.
     */
    struct ClassMemberInfo : ClassMemberInfoCommon {
      /// Implementations
      PSI_STD::vector<TreePtr<Implementation> > object_implementations;
      /// Static implementations
      PSI_STD::vector<TreePtr<Implementation> > static_implementations;

      template<typename V>
      static void visit(V& v) {
        visit_base<ClassMemberInfoCommon>(v);
        v("object_implementations", &ClassMemberInfo::object_implementations)
        ("static_implementations", &ClassMemberInfo::static_implementations);
      }
    };

    struct ClassMemberNamed : ClassMemberInfoCommon {
      String name;

      template<typename V>
      static void visit(V& v) {
        visit_base<ClassMemberInfoCommon>(v);
        v("name", &ClassMemberNamed::name);
      }
    };

    struct ClassInfo {
      /// Collection of all implementations in this class (since implementations do not have names)
      PSI_STD::vector<TreePtr<Implementation> > object_implementations;
      /// Collection of all static implementations in this class.
      PSI_STD::vector<TreePtr<Implementation> > static_implementations;
      /// List of members, which may or may not be named, however it is an error if non-empty names are not unique.
      PSI_STD::vector<ClassMemberNamed> members;

      template<typename V>
      static void visit(V& v) {
        v("object_implementations", &ClassInfo::object_implementations)
        ("static_implementations", &ClassInfo::static_implementations)
        ("members", &ClassInfo::members);
      }
    };

    struct ClassMacroMember {
      int index;
      TreePtr<MacroDotCallback> callback;
    };

    class ClassMacro : public Macro {
    public:
      static const MacroVtable vtable;

      typedef PSI_STD::map<String, ClassMacroMember> NameMapType;

      ClassMacro(CompileContext& compile_context,
                 const SourceLocation& location,
                 const NameMapType& members_)
      : Macro(compile_context, location),
      members(members_) {
        PSI_COMPILER_TREE_INIT();
      }

      NameMapType members;

      template<typename Visitor>
      static void visit_impl(Visitor& v) {
        visit_base<Macro>(v);
        v("members", &ClassMacro::members);
      }

      static TreePtr<Term> evaluate_impl(const ClassMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        NameMapType::const_iterator it = self.members.find("__call__");
        if (it == self.members.end())
          self.compile_context().error_throw(location, boost::format("Macro '%s' does not support evaluation") % self.location().logical->error_name(location.logical));

        TreePtr<Term> member_value;
        TreePtr<Term> evaluated = it->second.callback->dot(value, member_value, evaluate_context, location);
        TreePtr<Macro> macro = interface_lookup_as<Macro>(self.compile_context().macro_interface(), evaluated, location);
        return macro->evaluate(value, parameters, evaluate_context, location);
      }

      static TreePtr<Term> dot_impl(const ClassMacro& self,
                                    const TreePtr<Term>& value,
                                    const SharedPtr<Parser::Expression>& parameter,
                                    const TreePtr<EvaluateContext>& evaluate_context,
                                    const SourceLocation& location) {
        if (parameter->expression_type != Parser::expression_token)
          self.compile_context().error_throw(location, boost::format("Token following dot on '%s' is not a name") % self.location().logical->error_name(location.logical));

        const Parser::TokenExpression& token_expression = checked_cast<Parser::TokenExpression&>(*parameter);
        String member_name(token_expression.text.begin, token_expression.text.end);
        NameMapType::const_iterator it = self.members.find(member_name);

        if (it == self.members.end())
          self.compile_context().error_throw(location, boost::format("'%s' has no member named '%s'") % self.location().logical->error_name(location.logical) % member_name);

        TreePtr<Term> member_value;
        return it->second.callback->dot(value, member_value, evaluate_context, location);
      }
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

    class ClassMemberInfoTree : public Tree {
    public:
      static const TreeVtable vtable;

      ClassMemberInfoTree(CompileContext& compile_context, const SourceLocation& location, const ClassMemberInfo& member_info_)
      : Tree(compile_context, location),
      member_info(member_info_) {
      }

      ClassMemberInfo member_info;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("member_info", &ClassMemberInfoTree::member_info);
      }
    };
    
    const TreeVtable ClassMemberInfoTree::vtable = PSI_COMPILER_TREE(ClassMemberInfoTree, "psi.compiler.ClassMemberInfoTree", Tree);

    class ClassCompilerFinalTree : public Tree {
    public:
      static const TreeVtable vtable;

      ClassCompilerFinalTree(CompileContext& compile_context, const SourceLocation& location,
                             const TreePtr<Term>& object_term_,
                             const TreePtr<Term>& static_term_)
      : Tree(compile_context, location),
      object_term(object_term_),
      static_term(static_term_) {
      }

      TreePtr<Term> object_term;
      TreePtr<Term> static_term;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("object_term", &ClassCompilerFinalTree::object_term)
        ("static_term", &ClassCompilerFinalTree::static_term);
      }
    };

    const TreeVtable ClassCompilerFinalTree::vtable = PSI_COMPILER_TREE(ClassMemberInfoTree, "psi.compiler.ClassCompilerFinalTree", Tree);

    class ClassCompilerTree : public Tree {
    public:
      static const TreeVtable vtable;

      typedef PSI_STD::map<String, TreePtr<ClassMemberInfoTree> > NameMapType;

      ClassCompilerTree(CompileContext& compile_context,
                        const SourceLocation& location,
                        const TreePtr<ClassCompilerFinalTree>& final_,
                        const NameMapType& named_entries_)
      : Tree(compile_context, location),
      final(final_),
      named_entries(named_entries_) {
        PSI_COMPILER_TREE_INIT();
      }

      TreePtr<ClassCompilerFinalTree> final;
      NameMapType named_entries;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("final", &ClassCompilerTree::final)
        ("named_entries", &ClassCompilerTree::named_entries);
      }
    };
    
    const TreeVtable ClassCompilerTree::vtable = PSI_COMPILER_TREE(ClassCompilerTree, "psi.compiler.ClassCompilerTree", Tree);

    class ClassCompilerContext : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      ClassCompilerContext(const TreePtr<ClassCompilerTree>& class_compiler_,
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

      static LookupResult<TreePtr<Term> > lookup_impl(const ClassCompilerContext& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        if (name == "__class__")
          return self.class_compiler->final->object_term;

        ClassCompilerTree::NameMapType::const_iterator it = self.class_compiler->named_entries.find(name);
        if (it != self.class_compiler->named_entries.end()) {
          TreePtr<Term> member_value;
          return lookup_result_match(it->second->member_info.static_callback->dot
            (self.class_compiler->final->static_term, member_value, evaluate_context, location));
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable ClassCompilerContext::vtable = PSI_COMPILER_EVALUATE_CONTEXT(ClassCompilerContext, "psi.compiler.ClassCompilerContext", EvaluateContext);

    class ClassMemberCompiler {
      TreePtr<EvaluateContext> m_context;
      SharedPtr<Parser::Expression> m_expression;

    public:
      ClassMemberCompiler(const TreePtr<EvaluateContext>& context,
                          const SharedPtr<Parser::Expression>& expression)
      : m_context(context), m_expression(expression) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("context", &ClassMemberCompiler::m_context)
        ("expression", &ClassMemberCompiler::m_expression);
      }

      TreePtr<ClassMemberInfoTree> evaluate(const TreePtr<ClassMemberInfoTree>& self) {
        TreePtr<Term> expr = compile_expression(m_expression, m_context, self.location().logical);
        TreePtr<ClassMemberInfoCallback> callback = interface_lookup_as<ClassMemberInfoCallback>(self.compile_context().class_member_info_interface(), expr, self.location());
        return TreePtr<ClassMemberInfoTree>(new ClassMemberInfoTree(self.compile_context(), self.location(), callback->class_member_info()));
      }
    };

    /**
     * Contains final stage of class compilation.
     */
    class ClassCompilerFinal {
      ClassInfo m_info;
      PSI_STD::vector<TreePtr<ClassMutator> > m_mutators;
      PSI_STD::vector<std::pair<String, TreePtr<ClassMemberInfoTree> > > m_entries;
      ClassCompilerTree::NameMapType m_named_entries;

    public:
      typedef ClassCompilerFinalTree TreeType;
      
      ClassCompilerFinal(const ClassInfo& info,
                         const PSI_STD::vector<TreePtr<ClassMutator> >& mutators,
                         const PSI_STD::vector<std::pair<String, TreePtr<ClassMemberInfoTree> > >& entries,
                         const ClassCompilerTree::NameMapType& named_entries)
      : m_info(info),
      m_mutators(mutators),
      m_entries(entries),
      m_named_entries(named_entries) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("info", &ClassCompilerFinal::m_info)
        ("mutators", &ClassCompilerFinal::m_mutators)
        ("entries", &ClassCompilerFinal::m_entries)
        ("named_entries", &ClassCompilerFinal::m_named_entries);
      }

      TreePtr<ClassCompilerFinalTree> evaluate(const TreePtr<ClassCompilerFinalTree>& self) {
        // Add members to info
        for (PSI_STD::vector<std::pair<String, TreePtr<ClassMemberInfoTree> > >::const_iterator ii = m_entries.begin(), ie = m_entries.end(); ii != ie; ++ii) {
          const ClassMemberInfo& member_info = ii->second->member_info;
          m_info.object_implementations.insert(m_info.object_implementations.end(), member_info.object_implementations.begin(), member_info.object_implementations.end());
          m_info.static_implementations.insert(m_info.static_implementations.end(), member_info.static_implementations.begin(), member_info.static_implementations.end());
          ClassMemberNamed named_member;
          static_cast<ClassMemberInfoCommon&>(named_member) = member_info;
          named_member.name = ii->first;
          m_info.members.push_back(named_member);
        }
        
        // Run post-mutation
        for (PSI_STD::vector<TreePtr<ClassMutator> >::const_reverse_iterator ii = m_mutators.rbegin(), ie = m_mutators.rend(); ii != ie; ++ii)
          (*ii)->after(m_info);

        // Build class tree
        ClassMacro::NameMapType named_members;
        ClassMacro::NameMapType named_static_members;
        PSI_STD::vector<TreePtr<Term> > member_types;
        PSI_STD::vector<TreePtr<Term> > static_members;
        PSI_STD::vector<TreePtr<Term> > static_member_types;
        bool failed = false;
        for (PSI_STD::vector<ClassMemberNamed>::const_iterator ii = m_info.members.begin(), ie = m_info.members.end(); ii != ie; ++ii) {
          int member_index = -1, static_index = -1;
          
          if (ii->member_type) {
            member_index = member_types.size();
            member_types.push_back(ii->member_type);
          }
          
          if (ii->static_value) {
            static_index = static_members.size();
            static_members.push_back(ii->static_value);
            static_member_types.push_back(ii->static_value->type);
          }

          if (ii->member_callback) {
            if (named_members.find(ii->name) == named_members.end()) {
              ClassMacroMember m;
              m.index = member_index;
              m.callback = ii->member_callback;
              named_members.insert(std::make_pair(ii->name, m));
            } else {
              self.compile_context().error(self.location(), boost::format("Multiple object members named '%s'") % ii->name);
              failed = true;
            }
          }

          if (ii->static_callback) {
            if (named_static_members.find(ii->name) == named_static_members.end()) {
              ClassMacroMember m;
              m.index = static_index;
              m.callback = ii->member_callback;
              named_static_members.insert(std::make_pair(ii->name, m));
            } else {
              self.compile_context().error(self.location(), boost::format("Multiple static members named '%s'") % ii->name);
              failed = true;
            }
          }
        }

        if (failed)
          throw CompileException();

        TreePtr<StructType> object_type(new StructType(self.compile_context(), member_types, self.location()));
        PSI_STD::vector<TreePtr<Anonymous> > object_parameters;
        TreePtr<GenericType> object_generic(new GenericType(object_type, object_parameters, m_info.object_implementations, self.location()));
        TreePtr<Term> object_term;
        
        TreePtr<StructType> static_type(new StructType(self.compile_context(), static_member_types, self.location()));
        TreePtr<Term> static_value(new StructValue(static_type, static_members, self.location()));
        TreePtr<GenericType> static_generic(new GenericType(static_type, PSI_STD::vector<TreePtr<Anonymous> >(), m_info.static_implementations, self.location()));
        TreePtr<TypeInstance> static_instance(new TypeInstance(static_generic, default_, self.location()));
        TreePtr<Term> static_term(new TypeInstanceValue(static_instance, static_value, self.location()));

        return TreePtr<ClassCompilerFinalTree>(new ClassCompilerFinalTree(self.compile_context(), self.location(), object_term, static_term));
      }
    };

    class ClassCompiler {
      TreePtr<EvaluateContext> m_context;
      SharedPtr<Parser::TokenExpression> m_parameters;
      SharedPtr<Parser::TokenExpression> m_mutators;
      SharedPtr<Parser::TokenExpression> m_members;

    public:
      ClassCompiler(const TreePtr<EvaluateContext>& context,
                    const SharedPtr<Parser::TokenExpression>& parameters,
                    const SharedPtr<Parser::TokenExpression>& mutators,
                    const SharedPtr<Parser::TokenExpression>& members)
      : m_context(context), m_parameters(parameters), m_mutators(mutators), m_members(members) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("context", &ClassCompiler::m_context)
        ("parameters", &ClassCompiler::m_parameters)
        ("mutators", &ClassCompiler::m_mutators)
        ("members", &ClassCompiler::m_members);
      }

      TreePtr<ClassCompilerTree> evaluate(const TreePtr<ClassCompilerTree>& self) {
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

        TreePtr<EvaluateContext> member_context(new ClassCompilerContext(self, m_context));

        // Get member trees
        PSI_STD::vector<std::pair<String, TreePtr<ClassMemberInfoTree> > > entries;
        PSI_STD::map<String, TreePtr<ClassMemberInfoTree> > named_entries;
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
          TreePtr<ClassMemberInfoTree> entry = tree_callback<ClassMemberInfoTree>(compile_context, member_location, ClassMemberCompiler(member_context, named_expr.expression));
          entries.push_back(std::make_pair(expr_name, entry));

          if (named_expr.name)
            named_entries[expr_name] = entry;
        }

        TreePtr<ClassCompilerFinalTree> final_tree = tree_callback(compile_context, self.location(), ClassCompilerFinal(info, mutator_trees, entries, named_entries));
        return TreePtr<ClassCompilerTree>(new ClassCompilerTree(compile_context, self.location(), final_tree, named_entries));
      }
    };

    /**
     * Compile a function definition, and return a macro for invoking it.
     */
    TreePtr<Term> compile_class_definition(const List<SharedPtr<Parser::Expression> >& arguments,
                                           const TreePtr<EvaluateContext>& evaluate_context,
                                           const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context.compile_context();

      if ((arguments.size() < 1) || (arguments.size() > 3))
        compile_context.error_throw(location, boost::format("class macro expects one or two arguments, got %s") % arguments.size());

      SharedPtr<Parser::TokenExpression> parameters, mutators, members;
      if (arguments.size() == 2) {
        parameters = Parser::expression_as_token_type(arguments[0], Parser::TokenExpression::bracket);
        mutators = Parser::expression_as_token_type(arguments[1], Parser::TokenExpression::brace);
        
        if (!parameters && !mutators)
          compile_context.error_throw(location, "Optional argument to class definition is neither a (...) or a {...} so does not appear to specify either parameters or mutators");
      } else if (arguments.size() == 3) {
        if (!(parameters = Parser::expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
          compile_context.error_throw(location, "Parameter argument to class definition is not a (...)");

        if (!(mutators = Parser::expression_as_token_type(arguments[1], Parser::TokenExpression::brace)))
          compile_context.error_throw(location, "Mutator argument to class definition is not a {...}");
      }

      if (!(members = Parser::expression_as_token_type(arguments[arguments.size()-1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Members argument to class definition is not a [...]");

      return tree_callback<ClassCompilerTree>(compile_context, location, ClassCompiler(evaluate_context, parameters, mutators, members))->final->static_term;
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
