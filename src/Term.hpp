#ifndef HPP_PSI_COMPILER_TERM
#define HPP_PSI_COMPILER_TERM

#include "TreeBase.hpp"

namespace Psi {
  namespace Compiler {
    class Anonymous;
    
    struct TermVtable {
      TreeVtable base;
      const TreeBase* (*parameterize) (const Term*, const SourceLocation*, const void*, void*, unsigned);
      const TreeBase* (*specialize) (const Term*, const SourceLocation*, const void*, void*, unsigned);
      PsiBool (*match) (const Term*, const Term*, PSI_STD::vector<TreePtr<Term> >*, unsigned);
    };
    
    class Term : public Tree {
      friend class Metatype;

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;

      static const SIVtable vtable;
      static const bool match_unique = true;

      Term(const TermVtable *vtable, CompileContext& context, const SourceLocation& location);
      Term(const TermVtable *vtable, const TreePtr<Term>&, const SourceLocation&);

      /// \brief The type of this term.
      TreePtr<Term> type;

      bool is_type() const;

      /// \brief Replace anonymous terms in the list by parameters
      TreePtr<Term> parameterize(const SourceLocation& location, const List<TreePtr<Anonymous> >& elements, unsigned depth=0) const {
        return tree_from_base_take<Term>(derived_vptr(this)->parameterize(this, &location, elements.vptr(), elements.object(), depth));
      }

      /// \brief Replace parameter terms in this tree by given values
      TreePtr<Term> specialize(const SourceLocation& location, const List<TreePtr<Term> >& values, unsigned depth=0) const {
        return tree_from_base_take<Term>(derived_vptr(this)->specialize(this, &location, values.vptr(), values.object(), depth));
      }
      
      bool match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& match, unsigned depth) const;

      template<typename Visitor> static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("type", &Term::type);
      }
    };

    template<typename Derived>
    class RewriteVisitorBase {
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
        visit_members(*this, obj);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        *const_cast<TreePtr<T>*>(ptr[0]) = derived().visit_tree_ptr(*ptr[1]);
        if (*ptr[0] != *ptr[1])
          m_changed = true;
      }

      template<typename T>
      void visit_collection(const char*, const boost::array<const T*,2>& collections) {
        T *target = const_cast<T*>(collections[0]);
        for (typename T::const_iterator ii = collections[1]->begin(), ie = collections[1]->end(); ii != ie; ++ii) {
          typename T::value_type vt;
          boost::array<typename T::const_pointer, 2> m = {{&vt, &*ii}};
          visit_callback(*this, "", m);
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

    class GenericType;
    class JumpTarget;

    /**
     * Term visitor to perform pattern matching.
     */
    class MatchVisitor {
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
        visit_callback(*this, NULL, m);
      }

      void visit_object(const char*, const boost::array<const TreePtr<GenericType>*, 2>& ptr) {
        if (!result)
          return;
        result = (ptr[0] == ptr[1]);
      }
      
      void visit_object(const char*, const boost::array<const TreePtr<JumpTarget>*, 2>& ptr) {
        if (!result)
          return;
        result = (ptr[0] == ptr[1]);
      }
      
      template<typename T>
      bool visit_treeptr_helper(Term*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        return (*ptr[0])->match(*ptr[1], *m_wildcards, m_depth);
      }
      
      template<typename T>
      bool visit_treeptr_helper(Tree*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        return ptr[0]->get() == ptr[1]->get();
      }
      
      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        if (!result)
          return;

        result = visit_treeptr_helper(static_cast<T*>(0), ptr);
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
      List<TreePtr<Anonymous> > m_elements;
      unsigned m_depth;

      template<typename T>
      TreePtr<T> visit_tree_ptr_helper(const TreePtr<T>& ptr, const Tree*) {
        return ptr;
      }
      
      template<typename T>
      TreePtr<T> visit_tree_ptr_helper(const TreePtr<T>& ptr, const Term*) {
        if (ptr)
          return treeptr_cast<T>(ptr->parameterize(m_location, m_elements, m_depth));
        else
          return TreePtr<T>();
      }
      
    public:
      ParameterizeVisitor(const SourceLocation& location, const List<TreePtr<Anonymous> >& elements, unsigned depth)
      : m_location(location), m_elements(elements), m_depth(depth) {}

      template<typename T>
      TreePtr<T> visit_tree_ptr(const TreePtr<T>& ptr) {
        return visit_tree_ptr_helper(ptr, ptr.get());
      }
    };

    class SpecializeVisitor : public RewriteVisitorBase<SpecializeVisitor> {
      SourceLocation m_location;
      List<TreePtr<Term> > m_values;
      unsigned m_depth;

      template<typename T>
      TreePtr<T> visit_tree_ptr_helper(const TreePtr<T>& ptr, const Tree*) {
        return ptr;
      }

      template<typename T>
      TreePtr<T> visit_tree_ptr_helper(const TreePtr<T>& ptr, const Term*) {
        if (ptr)
          return treeptr_cast<T>(ptr->specialize(m_location, m_values, m_depth));
        else
          return TreePtr<T>();
      }

    public:
      SpecializeVisitor(const SourceLocation& location, const List<TreePtr<Term> >& values, unsigned depth)
      : m_location(location), m_values(values), m_depth(depth) {}

      template<typename T>
      TreePtr<T> visit_tree_ptr(const TreePtr<T>& ptr) {
        return visit_tree_ptr_helper(ptr, ptr.get());
      }
    };

    template<typename Derived>
    struct TermWrapper : NonConstructible {
      static PsiBool match(const Term *left, const Term *right, PSI_STD::vector<TreePtr<Term> > *wildcards, unsigned depth) {
        if (Derived::match_unique) {
          return left == right;
        } else {
          boost::array<const Derived*, 2> pair = {{static_cast<const Derived*>(left), static_cast<const Derived*>(right)}};
          MatchVisitor mv(wildcards, depth);
          visit_members(mv, pair);
          return mv.result;
        }
      }

      static const TreeBase* parameterize(const Term *self, const SourceLocation *location, const void *elements_vtable, void *elements_object, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<const Derived*>(self)}};
        ParameterizeVisitor pv(*location, List<TreePtr<Anonymous> >(elements_vtable, elements_object), depth);
        visit_members(pv, ptrs);
        return TreePtr<Derived>(pv.changed() ? new Derived(rewritten) : static_cast<const Derived*>(self)).release();
      }
      
      static const TreeBase* specialize(const Term *self, const SourceLocation *location, const void *values_vtable, void *values_object, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<const Derived*>(self)}};
        SpecializeVisitor pv(*location, List<TreePtr<Term> >(values_vtable, values_object), depth);
        visit_members(pv, ptrs);
        return TreePtr<Derived>(pv.changed() ? new Derived(rewritten) : static_cast<const Derived*>(self)).release();
      }
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TermWrapper<derived>::parameterize, \
    &::Psi::Compiler::TermWrapper<derived>::specialize, \
    &::Psi::Compiler::TermWrapper<derived>::match \
  }

    /**
     * \brief Base class for most types.
     *
     * Note that since types can be parameterized, a term not deriving from Type does
     * not mean that it is not a type, since type parameters are treated the same as
     * regular parameters. Use Term::is_type to determine whether a term is a type
     * or not.
     */
    class Type : public Term {
    public:
      static const SIVtable vtable;
      Type(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v) {visit_base<Term>(v);}
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_TERM(derived,name,super)

    /**
     * \brief Type of types.
     */
    class Metatype : public Term {
    public:
      static const TermVtable vtable;
      Metatype(CompileContext& compile_context, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };

    /// \brief Is this a type?
    inline bool Term::is_type() const {return !type || dyn_tree_cast<Metatype>(type.get());}

    /**
     * \brief Parameter to a Pattern.
     */
    class Parameter : public Term {
    public:
      static const TermVtable vtable;

      Parameter(CompileContext& compile_context, const SourceLocation& location);
      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned depth;
      /// Index of this parameter in its scope.
      unsigned index;
    };
  }
}

#endif
