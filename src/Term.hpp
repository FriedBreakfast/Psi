#ifndef HPP_PSI_COMPILER_TERM
#define HPP_PSI_COMPILER_TERM

#include "TreeBase.hpp"
#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    class Anonymous;
    class Term;
    class Statement;
    class ModuleGlobal;
    
    struct TermVtable {
      TreeVtable base;
      const TreeBase* (*parameterize) (const Term*, const SourceLocation*, const PSI_STD::vector<TreePtr<Anonymous> >*, unsigned);
      const TreeBase* (*specialize) (const Term*, const SourceLocation*, const PSI_STD::vector<TreePtr<Term> >*, unsigned);
      const TreeBase* (*anonymize) (const Term*, const SourceLocation*, PSI_STD::vector<TreePtr<Term> >*,
                                    PSI_STD::map<TreePtr<Statement>, unsigned>*, const PSI_STD::vector<TreePtr<Statement> >*, unsigned);
      PsiBool (*match) (const Term*, const Term*, PSI_STD::vector<TreePtr<Term> >*, unsigned);
      
      void (*global_dependencies) (const Term*, PSI_STD::set<TreePtr<ModuleGlobal> > *globals);
    };
    
    class Term : public Tree {
      friend class Metatype;

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;

      static const SIVtable vtable;
      /// \brief Used by vtable generator to determine whether matching is performed by a visitor or a custom function \c match_impl
      static const bool match_visit = false;
      /// \brief If matching is performed by a visitor, this determines whether to increment the parameter depth count.
      static const bool match_parameterized = false;
      /// \brief If parameterization is performed by a visitor
      static const bool specialize_visit = false;

      Term(const TermVtable *vtable, CompileContext& context, const SourceLocation& location);
      Term(const TermVtable *vtable, const TreePtr<Term>&, const SourceLocation&);

      /// \brief The type of this term.
      TreePtr<Term> type;

      bool is_type() const;

      /// \brief Replace anonymous terms in the list by parameters
      TreePtr<Term> parameterize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Anonymous> >& elements, unsigned depth=0) const {
        return tree_from_base_take<Term>(derived_vptr(this)->parameterize(this, &location, &elements, depth));
      }

      /// \brief Replace parameter terms in this tree by given values
      TreePtr<Term> specialize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, unsigned depth=0) const {
        return tree_from_base_take<Term>(derived_vptr(this)->specialize(this, &location, &values, depth));
      }
      
      TreePtr<Term> anonymize(const SourceLocation& location,
                              PSI_STD::vector<TreePtr<Term> >& parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned>& parameter_map,
                              const PSI_STD::vector<TreePtr<Statement> >& statements, unsigned depth) const {
        return tree_from_base_take<Term>(derived_vptr(this)->anonymize(this, &location, &parameter_types, &parameter_map, &statements, depth));
      }
      
      bool match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth) const;
      
      /**
       * \brief Find module-level globals on which this term depends.
       */
      void global_dependencies(PSI_STD::set<TreePtr<ModuleGlobal> >& globals) const {
        derived_vptr(this)->global_dependencies(this, &globals);
      }
      
      static bool match_impl(const Term& lhs, const Term& rhs, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth);
      static TreePtr<Term> parameterize_impl(const Term& self, const SourceLocation& location, const PSI_STD::vector<TreePtr<Anonymous> >& elements, unsigned depth);
      static TreePtr<Term> specialize_impl(const Term& self, const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, unsigned depth);
      static TreePtr<Term> anonymize_impl(const Term& self, const SourceLocation& location,
                                          PSI_STD::vector<TreePtr<Term> >& parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned>& parameter_map,
                                          const PSI_STD::vector<TreePtr<Statement> >& statements, unsigned depth);
      static void global_dependencies_impl(const Term& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);

      template<typename Visitor> static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("type", &Term::type);
      }
    };

    template<typename Derived>
    class RewriteVisitorBase : boost::noncopyable {
      bool m_changed;
      
      Derived& derived() {return *static_cast<Derived*>(this);}

    public:
      RewriteVisitorBase() : m_changed(false) {}

      bool changed() const {return m_changed;}

      template<typename T>
      void visit_base(const boost::array<T*,2>& c) {
        visit_members(derived(), c);
      }

      template<typename T>
      void visit_simple(const char*, const boost::array<const T*, 2>& obj) {
        *const_cast<T*>(obj[0]) = *obj[1];
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const T*, 2>& obj) {
        visit_members(derived(), obj);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        *const_cast<TreePtr<T>*>(ptr[0]) = treeptr_cast<T>(derived().visit_tree_ptr(*ptr[1]));
        if (ptr[0]->raw_get() != ptr[1]->raw_get())
          m_changed = true;
      }

      template<typename T>
      void visit_collection(const char*, const boost::array<const T*,2>& collections) {
        T *target = const_cast<T*>(collections[0]);
        for (typename T::const_iterator ii = collections[1]->begin(), ie = collections[1]->end(); ii != ie; ++ii) {
          typename T::value_type vt;
          boost::array<typename T::const_pointer, 2> m = {{&vt, &*ii}};
          visit_callback(derived(), "", m);
          target->insert(target->end(), vt);
        }
      }

      template<typename T>
      void visit_sequence(const char*, const boost::array<T*,2>& collections) {
        visit_collection(NULL, collections);
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*, 2>& collections) {
        visit_collection(NULL, collections);
      }
    };

    /**
     * Term visitor to perform pattern matching.
     */
    class MatchVisitor : boost::noncopyable {
      PSI_STD::vector<TreePtr<Term> > *m_wildcards;
      unsigned m_depth;

    public:
      bool result;

      MatchVisitor(PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth)
      : m_wildcards(wildcards), m_depth(depth), result(true) {}

      template<typename T>
      void visit_base(const boost::array<T*,2>& c) {
        visit_members(*this, c);
      }
      
      template<typename T>
      bool do_visit_base(VisitorTag<T>) {
        return true;
      }

      template<typename T>
      void visit_simple(const char*, const boost::array<T*, 2>& obj) {
        if (!result)
          return;
        result = *obj[0] == *obj[1];
      }

      template<typename T>
      void visit_object(const char*, const boost::array<T*, 2>& obj) {
        if (!result)
          return;
        visit_members(*this, obj);
      }

      /// Simple pointers are assumed to be owned by this object
      template<typename T>
      void visit_object(const char*, const boost::array<T*const*, 2>& obj) {
        if (!result)
          return;

        boost::array<T*, 2> m = {{*obj[0], *obj[1]}};
        if (!*m[0])
          result = !*m[1];
        else if (!*m[1])
          result = false;
        else
          visit_callback(*this, NULL, m);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        if (!result)
          return;

        if (!*ptr[0])
          result = !*ptr[1];
        else if (!*ptr[1])
          result = false;
        else
          result = (*ptr[0])->match(*ptr[1], *m_wildcards, m_depth);
      }

      template<typename T>
      void visit_sequence (const char*, const boost::array<T*,2>& collections) {
        if (!result)
          return;

        if (collections[0]->size() != collections[1]->size()) {
          result = false;
        } else {
          typename T::const_iterator ii = collections[0]->begin(), ie = collections[0]->end(),
          ji = collections[1]->begin(), je = collections[1]->end();

          for (; (ii != ie) && (ji != je); ++ii, ++ji) {
            boost::array<typename T::const_pointer, 2> m = {{&*ii, &*ji}};
            visit_callback(*this, "", m);

            if (!result)
              return;
          }

          if ((ii != ie) || (ji != je))
            result = false;
        }
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*, 2>& maps) {
        if (!result)
          return;

        if (maps[0]->size() != maps[1]->size()) {
          result = false;
          return;
        }

        for (typename T::const_iterator ii = maps[0]->begin(), ie = maps[0]->end(), je = maps[1]->end(); ii != ie; ++ii) {
          typename T::const_iterator ji = maps[1]->find(ii->first);
          if (ji == je) {
            result = false;
            return;
          }

          boost::array<typename T::const_pointer, 2> v = {{&*ii, &*ji}};
          visit_callback(*this, NULL, v);
          if (!result)
            return;
        }
      }
    };


    class ParameterizeVisitor : public RewriteVisitorBase<ParameterizeVisitor> {
      SourceLocation m_location;
      const PSI_STD::vector<TreePtr<Anonymous> > *m_elements;
      unsigned m_depth;
      
    public:
      ParameterizeVisitor(const SourceLocation& location, const PSI_STD::vector<TreePtr<Anonymous> > *elements, unsigned depth)
      : m_location(location), m_elements(elements), m_depth(depth) {}

      TreePtr<Term> visit_tree_ptr(const TreePtr<Term>& ptr) {
        return ptr ? ptr->parameterize(m_location, *m_elements, m_depth) : TreePtr<Term>();
      }
    };

    class SpecializeVisitor : public RewriteVisitorBase<SpecializeVisitor> {
      SourceLocation m_location;
      const PSI_STD::vector<TreePtr<Term> > *m_values;
      unsigned m_depth;

    public:
      SpecializeVisitor(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> > *values, unsigned depth)
      : m_location(location), m_values(values), m_depth(depth) {}
      
      TreePtr<Term> visit_tree_ptr(const TreePtr<Term>& ptr) {
        return ptr ? ptr->specialize(m_location, *m_values, m_depth) : TreePtr<Term>();
      }
    };
    
    class AnonymizeVisitor : public RewriteVisitorBase<AnonymizeVisitor> {
      SourceLocation m_location;
      PSI_STD::vector<TreePtr<Term> > *m_parameter_types;
      PSI_STD::map<TreePtr<Statement>, unsigned> *m_parameter_map;
      const PSI_STD::vector<TreePtr<Statement> > *m_statements;
      unsigned m_depth;

    public:
      AnonymizeVisitor(const SourceLocation& location,
                       PSI_STD::vector<TreePtr<Term> > *parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned> *parameter_map,
                       const PSI_STD::vector<TreePtr<Statement> > *statements, unsigned depth)
      : m_location(location), m_parameter_types(parameter_types), m_parameter_map(parameter_map), m_statements(statements), m_depth(depth) {}

      TreePtr<Term> visit_tree_ptr(const TreePtr<Term>& ptr) {
        return ptr ? ptr->anonymize(m_location, *m_parameter_types, *m_parameter_map, *m_statements, m_depth) : TreePtr<Term>();
      }
    };
    
    template<typename Derived>
    class UnaryTreePtrVisitor : boost::noncopyable {
      Derived& derived() {return *static_cast<Derived*>(this);}
      
    public:
      template<typename T>
      void visit_base(const boost::array<T*,1>& c) {
        visit_members(derived(), c);
      }

      template<typename T>
      void visit_simple(const char*, const boost::array<const T*, 1>&) {}

      template<typename T>
      void visit_object(const char*, const boost::array<const T*, 1>& obj) {
        visit_members(derived(), obj);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 1>& ptr) {
        derived().visit_tree_ptr(*ptr[0]);
      }

      template<typename T>
      void visit_collection(const char*, const boost::array<const T*,1>& collections) {
        for (typename T::const_iterator ii = collections[0]->begin(), ie = collections[0]->end(); ii != ie; ++ii) {
          boost::array<typename T::const_pointer, 1> m = {{&*ii}};
          visit_callback(derived(), NULL, m);
        }
      }

      template<typename T>
      void visit_sequence(const char*, const boost::array<T*,1>& collections) {
        visit_collection(NULL, collections);
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*, 1>& collections) {
        visit_collection(NULL, collections);
      }
    };

    class GlobalDependenciesVisitor : public UnaryTreePtrVisitor<GlobalDependenciesVisitor> {
      PSI_STD::set<TreePtr<ModuleGlobal> > *m_dependencies;
      
    public:
      GlobalDependenciesVisitor(PSI_STD::set<TreePtr<ModuleGlobal> > *dependencies) : m_dependencies(dependencies) {}
      
      template<typename T>
      void visit_tree_ptr(const TreePtr<T>& ptr) {
        if (ptr)
          ptr->global_dependencies(*m_dependencies);
      }
    };
    
    template<typename Derived>
    struct TermWrapper : NonConstructible {
      /**
       * Match implementation when visitor is not used.
       */
      static PsiBool match_helper(static_bool<false>, const Term *left, const Term *right, PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth) {
        return Derived::match_impl(*static_cast<const Derived*>(left), *static_cast<const Derived*>(right), *wildcards, depth);
      }

      /**
       * Match implementation when visitor is used.
       */
      static PsiBool match_helper(static_bool<true>, const Term *left, const Term *right, PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth) {
        boost::array<const Derived*, 2> pair = {{static_cast<const Derived*>(left), static_cast<const Derived*>(right)}};
        MatchVisitor mv(wildcards, depth + (Derived::match_parameterized?1:0));
        visit_members(mv, pair);
        return mv.result;
      }
      
      static PsiBool match(const Term *left, const Term *right, PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth) {
        return match_helper(static_bool<Derived::match_visit>(), left, right, wildcards, depth);
      }
      
      static const TreeBase* parameterize_helper(static_bool<false>, const Term *self, const SourceLocation *location, const PSI_STD::vector<TreePtr<Anonymous> > *elements, unsigned depth) {
        return Derived::parameterize_impl(*static_cast<const Derived*>(self), *location, *elements, depth).release();
      }
      
      static const TreeBase* parameterize_helper(static_bool<true>, const Term *self, const SourceLocation *location, const PSI_STD::vector<TreePtr<Anonymous> > *elements, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<const Derived*>(self)}};
        ParameterizeVisitor pv(*location, elements, depth + (Derived::match_parameterized?1:0));
        visit_members(pv, ptrs);
        return TreePtr<Derived>(pv.changed() ? new Derived(rewritten) : static_cast<const Derived*>(self)).release();
      }

      static const TreeBase* parameterize(const Term *self, const SourceLocation *location, const PSI_STD::vector<TreePtr<Anonymous> > *elements, unsigned depth) {
        return parameterize_helper(static_bool<Derived::match_visit>(), self, location, elements, depth);
      }

      static const TreeBase* specialize_helper(static_bool<false>, const Term *self, const SourceLocation *location, const PSI_STD::vector<TreePtr<Term> > *values, unsigned depth) {
        return Derived::specialize_impl(*static_cast<const Derived*>(self), *location, *values, depth).release();
      }

      static const TreeBase* specialize_helper(static_bool<true>, const Term *self, const SourceLocation *location, const PSI_STD::vector<TreePtr<Term> > *values, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<const Derived*>(self)}};
        SpecializeVisitor pv(*location, values, depth + (Derived::match_parameterized?1:0));
        visit_members(pv, ptrs);
        return TreePtr<Derived>(pv.changed() ? new Derived(rewritten) : static_cast<const Derived*>(self)).release();
      }

      static const TreeBase* specialize(const Term *self, const SourceLocation *location, const PSI_STD::vector<TreePtr<Term> > *values, unsigned depth) {
        return specialize_helper(static_bool<Derived::specialize_visit>(), self, location, values, depth);
      }

      static const TreeBase* anonymize_helper(static_bool<false>, const Term *self, const SourceLocation *location,
                                              PSI_STD::vector<TreePtr<Term> > *parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned> *parameter_map,
                                              const PSI_STD::vector<TreePtr<Statement> > *statements, unsigned depth) {
        return Derived::anonymize_impl(*static_cast<const Derived*>(self), *location, *parameter_types, *parameter_map, *statements, depth).release();
      }

      static const TreeBase* anonymize_helper(static_bool<true>, const Term *self, const SourceLocation *location,
                                              PSI_STD::vector<TreePtr<Term> > *parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned> *parameter_map,
                                              const PSI_STD::vector<TreePtr<Statement> > *statements, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<const Derived*>(self)}};
        AnonymizeVisitor av(*location, parameter_types, parameter_map, statements, depth + (Derived::match_parameterized?1:0));
        visit_members(av, ptrs);
        return TreePtr<Derived>(av.changed() ? new Derived(rewritten) : static_cast<const Derived*>(self)).release();
      }

      static const TreeBase* anonymize(const Term *self, const SourceLocation *location,
                                       PSI_STD::vector<TreePtr<Term> > *parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned> *parameter_map,
                                       const PSI_STD::vector<TreePtr<Statement> > *statements, unsigned depth) {
        return anonymize_helper(static_bool<Derived::match_visit>(), self, location, parameter_types, parameter_map, statements, depth);
      }

      static void global_dependencies_helper(static_bool<false>, const Term *self, PSI_STD::set<TreePtr<ModuleGlobal> > *globals) {
        Derived::global_dependencies_impl(*static_cast<const Derived*>(self), *globals);
      }
      
      static void global_dependencies_helper(static_bool<true>, const Term *self, PSI_STD::set<TreePtr<ModuleGlobal> > *globals) {
        GlobalDependenciesVisitor gv(globals);
        boost::array<const Derived*,1> ptrs = {{static_cast<const Derived*>(self)}};
        visit_members(gv, ptrs);
      }
      
      static void global_dependencies(const Term *self, PSI_STD::set<TreePtr<ModuleGlobal> > *globals) {
        return global_dependencies_helper(static_bool<Derived::match_visit>(), self, globals);
      }
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TermWrapper<derived>::parameterize, \
    &::Psi::Compiler::TermWrapper<derived>::specialize, \
    &::Psi::Compiler::TermWrapper<derived>::anonymize, \
    &::Psi::Compiler::TermWrapper<derived>::match, \
    &::Psi::Compiler::TermWrapper<derived>::global_dependencies \
  }

    /**
     * \brief Base class for (most) functional values.
     * 
     * Apart from built-in function calls, all terms which only take pure
     * functional arguments derive from this.
     */
    class Functional : public Term  {
    public:
      static const SIVtable vtable;
      static const bool match_visit = true;
      static const bool specialize_visit = true;
      Functional(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
      Functional(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };

    /**
     * \brief Base class for most types.
     *
     * Note that since types can be parameterized, a term not deriving from Type does
     * not mean that it is not a type, since type parameters are treated the same as
     * regular parameters. Use Term::is_type to determine whether a term is a type
     * or not.
     */
    class Type : public Functional {
    public:
      static const SIVtable vtable;
      Type(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v) {visit_base<Term>(v);}
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_TERM(derived,name,super)

    /**
     * \brief Type of types.
     */
    class Metatype : public Functional {
    public:
      static const TermVtable vtable;
      Metatype(CompileContext& compile_context, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };

    /**
     * \brief Is this a type?
     * 
     * This means "can this be the type of another term". Therefore, Metatype
     * counts as a type here.
     */
    inline bool Term::is_type() const {return !type || tree_isa<Metatype>(type);}

    /**
     * Anonymous term. Has a type but no defined value.
     *
     * The value must be defined elsewhere, for example by being part of a function.
     */
    class Anonymous : public Term {
    public:
      static const TermVtable vtable;

      Anonymous(CompileContext& compile_context, const SourceLocation& location);
      Anonymous(const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      static TreePtr<Term> parameterize_impl(const Anonymous& self, const SourceLocation&, const PSI_STD::vector<TreePtr<Anonymous> >&, unsigned depth);
    };

    /**
     * \brief Parameter to a Pattern.
     */
    class Parameter : public Term {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;

      Parameter(CompileContext& compile_context, const SourceLocation& location);
      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned depth;
      /// Index of this parameter in its scope.
      unsigned index;
      
      static TreePtr<Term> specialize_impl(const Parameter& self, const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, unsigned depth);
    };

    TreePtr<Term> anonymize_term(const TreePtr<Term>& term, const SourceLocation& location,
                                 const PSI_STD::vector<TreePtr<Statement> >& statements=PSI_STD::vector<TreePtr<Statement> >());
    TreePtr<Term> anonymize_term_delayed(const TreePtr<Term>& term, const SourceLocation& location, const PSI_STD::vector<TreePtr<Statement> >& statements=PSI_STD::vector<TreePtr<Statement> >());
    TreePtr<Term> anonymize_type_delayed(const TreePtr<Term>& term, const SourceLocation& location, const PSI_STD::vector<TreePtr<Statement> >& statements=PSI_STD::vector<TreePtr<Statement> >());
  }
}

#endif
