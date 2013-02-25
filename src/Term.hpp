#ifndef HPP_PSI_COMPILER_TERM
#define HPP_PSI_COMPILER_TERM

#include "TreeBase.hpp"
#include "Enums.hpp"

namespace Psi {
  namespace Compiler {
    class Anonymous;
    class Term;
    class Statement;
    class ModuleGlobal;
    
    class TermVisitor;
    
    struct TermVisitorVtable {
      SIVtable base;
      void (*visit) (TermVisitor*, const TreePtr<Term>*);
    };
    
    class TermVisitor : public SIBase {
    public:
      static const SIVtable vtable;
      typedef TermVisitorVtable VtableType;
      
      TermVisitor(const VtableType *vptr) {PSI_COMPILER_SI_INIT(vptr);}
      
      void visit(const TreePtr<Term>& value) {
        derived_vptr(this)->visit(this, &value);
      }
    };
    
    template<typename Derived>
    struct TermVisitorWrapper : NonConstructible {
      static void visit(TermVisitor *self, const TreePtr<Term> *term) {
        return Derived::visit_impl(*static_cast<Derived*>(self), *term);
      }
    };
    
#define PSI_COMPILER_TERM_VISITOR(cls,name,base) { \
      PSI_COMPILER_SI(name,&base::vtable), \
      &::Psi::Compiler::TermVisitorWrapper<cls>::visit \
    }
    
    class TermComparator;
    
    struct TermComparatorVtable {
      SIVtable base;
      PsiBool (*compare) (TermComparator*, const TreePtr<Term>*, const TreePtr<Term>*);
    };
    
    class TermComparator : public SIBase {
    public:
      static const SIVtable vtable;
      typedef TermComparatorVtable VtableType;
      
      TermComparator(const VtableType *vptr) {PSI_COMPILER_SI_INIT(vptr);}
      
      bool compare(const TreePtr<Term>& lhs, const TreePtr<Term>& rhs) {
        return derived_vptr(this)->compare(this, &lhs, &rhs);
      }
    };
    
    template<typename Derived>
    struct TermComparatorWrapper : NonConstructible {
      static PsiBool compare(TermComparator *self, const TreePtr<Term> *lhs, const TreePtr<Term> *rhs) {
        return Derived::compare_impl(*static_cast<Derived*>(self), *lhs, *rhs);
      }
    };
    
#define PSI_COMPILER_TERM_COMPARATOR(cls,name,base) { \
      PSI_COMPILER_SI(name,&base::vtable), \
      &::Psi::Compiler::TermComparatorWrapper<cls>::compare \
    }
    
    class TermRewriter;    
    
    struct TermRewriterVtable {
      SIVtable base;
      void (*rewrite) (TreePtr<Term>*, TermRewriter*, const TreePtr<Term>*);
    };
    
    class TermRewriter : public SIBase {
    public:
      static const SIVtable vtable;
      typedef TermRewriterVtable VtableType;
      
      TermRewriter(const VtableType *vptr) {PSI_COMPILER_SI_INIT(vptr);}
      
      TreePtr<Term> rewrite(const TreePtr<Term>& value) {
        ResultStorage<TreePtr<Term> > r;
        derived_vptr(this)->rewrite(r.ptr(), this, &value);
        return r.done();
      }
    };
    
    template<typename Derived>
    struct TermRewriterWrapper : NonConstructible {
      static void rewrite(TreePtr<Term> *out, TermRewriter *self, const TreePtr<Term> *value) {
        new (out) TreePtr<Term> (Derived::rewrite_impl(*static_cast<Derived*>(self), *value));
      }
    };

#define PSI_COMPILER_TERM_REWRITER(cls,name,base) { \
    PSI_COMPILER_SI(name,&base::vtable), \
    &::Psi::Compiler::TermRewriterWrapper<cls>::rewrite \
  }
    
    class TermVisitorVisitor : public ObjectVisitorBase<TermVisitorVisitor> {
      TermVisitor *m_v;
      
    public:
      TermVisitorVisitor(TermVisitor *v) : m_v(v) {}
      template<typename T> void visit_object_ptr(const ObjectPtr<T>&) {}
      template<typename T> void visit_tree_ptr(const TreePtr<T>& ptr) {m_v->visit(ptr);}
      template<typename T, typename U> void visit_delayed(DelayedValue<T,U>& ptr) {m_v->visit(ptr.get_checked());}
      template<typename T> boost::true_type do_visit_base(VisitorTag<T>) {return boost::true_type();}
      boost::false_type do_visit_base(VisitorTag<Object>) {return boost::false_type();}
    };
    
    struct TermVtable {
      TreeVtable base;
      void (*visit) (const Term*, TermVisitor*);
    };
    
    struct TermResultType {
      /// Result type
      TreePtr<Term> type;
      /// Result storage mode
      ResultMode mode;
      /// Whether this term requires any evaluation
      PsiBool pure;
      /// What sort of type this is; if it is a type.
      TypeMode type_mode;
      
      template<typename V>
      static void visit(V& v) {
        v("type", &TermResultType::type)
        ("mode", &TermResultType::mode)
        ("pure", &TermResultType::pure);
      }
    };
        
    class Term : public Tree {
      friend class Functional;
      Term(const TermVtable *vptr);
      friend class Statement;
      Term(const TermVtable *vptr, CompileContext& compile_context, const TermResultType& type, const SourceLocation& location);

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;

      static const SIVtable vtable;
      Term(const TermVtable *vtable, const TermResultType& result_type, const SourceLocation& location);

      /// \brief The result type and mode of this term.
      TermResultType result_type;

      /**
       * \brief Is this a type?
       * 
       * This means "can this be the type of another term". Therefore, Metatype
       * counts as a type here.
       */
      bool is_type() const {return result_type.type_mode != type_mode_none;}
      
      bool match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth) const;
      TreePtr<Term> parameterize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Anonymous> >& elements) const;
      TreePtr<Term> specialize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values) const;
      TreePtr<Term> anonymize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Statement> >& statements) const;
      TreePtr<Term> anonymize(const SourceLocation& location) const;

      /**
       * Visit all terms referenced by this term.
       */
      void visit_terms(TermVisitor& visitor) const {
        derived_vptr(this)->visit(this, &visitor);
      }

      template<typename Visitor> static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("result_type", &Term::result_type);
      }
      
      template<typename Derived>
      static void visit_terms_impl(const Derived& self, TermVisitor& v) {
        boost::array<Derived*, 1> ptrs = {{const_cast<Derived*>(&self)}};
        TermVisitorVisitor vv(&v);
        visit_members(vv, ptrs);
      }
    };

    template<typename Derived>
    struct TermWrapper : NonConstructible {
      static void visit(const Term *self, TermVisitor *visitor) {
        Derived::visit_terms_impl(*self, *visitor);
      }
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TermWrapper<derived>::visit \
  }

    class TermRewriterVisitor : public ObjectVisitorBase<TermRewriterVisitor> {
      TermRewriter *m_rw;
      
    public:
      TermRewriterVisitor(TermRewriter *rw) : m_rw(rw) {}
      
      template<typename T> void visit_tree_ptr_helper(TreePtr<T>&, Tree*) {}
      
      template<typename T>
      void visit_tree_ptr_helper(TreePtr<T>& ptr, Term*) {
        ptr = treeptr_cast<T>(m_rw->rewrite(ptr));
      }
      
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        visit_tree_ptr_helper(ptr, static_cast<T*>(NULL));
      }
      
      template<typename T, typename U>
      void visit_delayed(DelayedValue<T,U>& ptr) {
        boost::array<T*,1> ptrs = {{&ptr.get_checked()}};
        visit_callback(this, NULL, ptrs);
      }
    };
    
    template<typename T>
    class FunctionalHashVisitor : boost::noncopyable {
      std::size_t *m_result;
      const T *m_ptr;
    public:
      FunctionalHashVisitor(std::size_t *result, const T *ptr) : m_result(result), m_ptr(ptr) {}
      template<typename U> FunctionalHashVisitor& operator () (const char*,U T::*member) {boost::hash_combine(*m_result, m_ptr->*member); return *this;}
      friend void visit_base_hook(FunctionalHashVisitor&, VisitorTag<Term>) {}
      template<typename U> friend void visit_base_hook(FunctionalHashVisitor& v, VisitorTag<U>) {FunctionalHashVisitor<U> w(v.m_result, v.m_ptr); U::visit(w);}
    };
    
    template<typename T>
    class FunctionalEquivalentVisitor : boost::noncopyable {
      const T *m_lhs, *m_rhs;
    public:
      bool equivalent;
      FunctionalEquivalentVisitor(const T *lhs, const T *rhs) : m_lhs(lhs), m_rhs(rhs), equivalent(true) {}
      template<typename U> FunctionalEquivalentVisitor& operator () (const char*,U T::*member) {if (equivalent) equivalent = (m_lhs->*member == m_rhs->*member); return *this;}
      friend void visit_base_hook(FunctionalEquivalentVisitor&, VisitorTag<Term>) {}
      template<typename U> friend void visit_base_hook(FunctionalEquivalentVisitor& v, VisitorTag<U>) {
        if (v.equivalent) {
          FunctionalEquivalentVisitor<U> w(v.m_lhs, v.m_rhs);
          U::visit(w);
          v.equivalent = w.equivalent;
        }
      }
    };
    
    /**
     * Term visitor to comparison.
     */
    class TermComparatorVisitor : boost::noncopyable {
      TermComparator *m_v;

    public:
      bool result;

      TermComparatorVisitor(TermComparator *v) : m_v(v), result(true) {}

      void visit_base(const boost::array<Object*,2>&) {}

      template<typename T>
      void visit_base(const boost::array<T*,2>& c) {
        visit_members(*this, c);
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
      void visit_object(const char*, const boost::array<T**, 2>& obj) {
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
      bool treeptr_compare(const TreePtr<T>& lhs, const TreePtr<T>& rhs, Tree*) {
        return lhs == rhs;
      }

      template<typename T>
      bool treeptr_compare(const TreePtr<T>& lhs, const TreePtr<T>& rhs, Term*) {
        return m_v->compare(lhs, rhs);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<TreePtr<T>*, 2>& ptr) {
        if (!result)
          return;

        if (!*ptr[0])
          result = !*ptr[1];
        else if (!*ptr[1])
          result = false;
        else
          result = treeptr_compare(*ptr[0], *ptr[1], static_cast<T*>(NULL));
      }

    private:
      template<typename T, typename U>
      void visit_object(const char*, const boost::array<ObjectPtr<T>*, 2>& ptr);
      template<typename T, typename U>
      void visit_object(const char*, const boost::array<DelayedValue<T,U>*, 2>& ptr);

    public:
      template<typename T>
      void visit_sequence (const char*, const boost::array<T*,2>& collections) {
        if (!result)
          return;

        if (collections[0]->size() != collections[1]->size()) {
          result = false;
        } else {
          typename T::iterator ii = collections[0]->begin(), ie = collections[0]->end(),
          ji = collections[1]->begin(), je = collections[1]->end();

          for (; (ii != ie) && (ji != je); ++ii, ++ji) {
            boost::array<typename T::pointer, 2> m = {{&*ii, &*ji}};
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

        for (typename T::iterator ii = maps[0]->begin(), ie = maps[0]->end(), je = maps[1]->end(); ii != ie; ++ii) {
          typename T::iterator ji = maps[1]->find(ii->first);
          if (ji == je) {
            result = false;
            return;
          }

          boost::array<typename T::pointer, 2> v = {{&*ii, &*ji}};
          visit_callback(*this, NULL, v);
          if (!result)
            return;
        }
      }
    };
    
    class Functional;
    
    struct FunctionalVtable {
      TermVtable base;
      void (*simplify) (TreePtr<Term>*, const Functional*);
      std::size_t (*hash) (const Functional*);
      PsiBool (*equivalent) (const Functional*, const Functional*);
      void (*check_type) (TermResultType*, const Functional*);
      void (*rewrite) (TreePtr<Term>*, const Functional*,TermRewriter*,const SourceLocation*);
      PsiBool (*compare) (const Functional*,const Functional*,TermComparator*);
    };
    
    /**
     * \brief Base class for (most) functional values.
     * 
     * Apart from built-in function calls, all terms which only take pure
     * functional arguments derive from this.
     */
    class Functional : public Term  {
      std::size_t m_hash;

    public:
      typedef FunctionalVtable VtableType;
      static const SIVtable vtable;
      Functional(const VtableType *vptr);
      template<typename V> static void visit(V& v);
      
      /**
       * Simplify this term.
       * 
       * The term may be on the stack when this is called, so if
       * no simplification is possible this routine will return NULL.
       */
      TreePtr<Term> simplify() const {
        ResultStorage<TreePtr<Term> > rs;
        derived_vptr(this)->simplify(rs.ptr(), this);
        return rs.done();
      }
      
      std::size_t compute_hash() const {
        return derived_vptr(this)->hash(this);
      }
      
      bool equivalent(const Functional& other) const {
        PSI_ASSERT(si_vptr(this) == si_vptr(&other));
        return derived_vptr(this)->equivalent(this, &other);
      }
      
      TreePtr<Term> rewrite(TermRewriter& rewriter, const SourceLocation& location) const {
        ResultStorage<TreePtr<Term> > rs;
        derived_vptr(this)->rewrite(rs.ptr(), this, &rewriter, &location);
        return rs.done();
      }
      
      bool compare(const Functional& other, TermComparator& cmp) const {
        PSI_ASSERT(si_vptr(this) == si_vptr(&other));
        return derived_vptr(this)->compare(this, &other, &cmp);
      }
      
      static TreePtr<Term> simplify_impl(const Functional&) {
        return TreePtr<Term>();
      }
      
      template<typename Derived>
      static std::size_t hash_impl(const Derived& self) {
        std::size_t hash = 0;
        FunctionalHashVisitor<Derived> hv(&hash, &self);
        Derived::visit(hv);
        return hash;
      }
      
      template<typename Derived>
      static bool equivalent_impl(const Derived& lhs, const Derived& rhs) {
        FunctionalEquivalentVisitor<Derived> ev(&lhs, &rhs);
        Derived::visit(ev);
        return ev.equivalent;
      }
      
      template<typename Derived>
      static TreePtr<Term> rewrite_impl(const Derived& self, TermRewriter& rewriter, const SourceLocation& location) {
        TermRewriterVisitor rw(&rewriter);
        Derived copy(self);
        boost::array<Derived*, 1> ptr = {{&copy}};
        visit_members(rw, ptr);
        return self.compile_context().get_functional(copy, location);
      }
      
      template<typename Derived>
      static bool compare_impl(const Derived& self, const Derived& other, TermComparator& cmp) {
        TermComparatorVisitor cv(&cmp);
        boost::array<Derived*, 2> ptrs = {{const_cast<Derived*>(&self), const_cast<Derived*>(&other)}};
        visit_members(cv, ptrs);
        return cv.result;
      }
      
    private:
      friend class Metatype;
      Functional(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
    };
    
    template<typename Derived>
    struct FunctionalWrapper : NonConstructible {
      static void simplify(TreePtr<Term> *out, const Functional *self) {
        new (out) TreePtr<Term> (Derived::simplify_impl(*static_cast<const Derived*>(self)));
      }
      
      static std::size_t hash(const Functional *self) {
        return Derived::hash_impl(*static_cast<const Derived*>(self));
      }
      
      static PsiBool equivalent(const Functional *lhs, const Functional *rhs) {
        return Derived::equivalent_impl(*static_cast<const Derived*>(lhs), *static_cast<const Derived*>(rhs));
      }
      
      static void check_type(TermResultType *out, const Functional *self) {
        new (out) TermResultType (Derived::check_type_impl(*static_cast<const Derived*>(self)));
      }
      
      static void rewrite(TreePtr<Term> *out, const Functional *self, TermRewriter *cmp, const SourceLocation *location) {
        new (out) TreePtr<Term> (Derived::rewrite_impl(*static_cast<const Derived*>(self), *cmp, *location));
      }
      
      static PsiBool compare(const Functional *lhs, const Functional *rhs, TermComparator* cmp) {
        return Derived::compare_impl(*static_cast<const Derived*>(lhs), *static_cast<const Derived*>(rhs), *cmp);
      }
    };
    
#define PSI_COMPILER_FUNCTIONAL(derived,name,super) { \
    PSI_COMPILER_TERM(derived,name,super), \
    &::Psi::Compiler::FunctionalWrapper<derived>::simplify, \
    &::Psi::Compiler::FunctionalWrapper<derived>::hash, \
    &::Psi::Compiler::FunctionalWrapper<derived>::equivalent, \
    &::Psi::Compiler::FunctionalWrapper<derived>::check_type, \
    &::Psi::Compiler::FunctionalWrapper<derived>::rewrite, \
    &::Psi::Compiler::FunctionalWrapper<derived>::compare \
  }

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
      Type(const VtableType *vptr);
      template<typename Visitor> static void visit(Visitor& v) {visit_base<Term>(v);}
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_FUNCTIONAL(derived,name,super)

    /**
     * \brief Type of types.
     */
    class Metatype : public Functional {
    public:
      static const VtableType vtable;
      Metatype();
      template<typename V> static void visit(V& v);
      static TermResultType check_type_impl(const Metatype& self);
    };

    /**
     * Anonymous term. Has a type but no defined value.
     *
     * The value must be defined elsewhere, for example by being part of a function.
     */
    class Anonymous : public Term {
      static TermResultType make_result_type(const TreePtr<Term>& type, ResultMode mode, const SourceLocation& location);
    public:
      static const VtableType vtable;

      Anonymous(const TreePtr<Term>& type, ResultMode mode, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };

    /**
     * \brief Parameter to a Pattern.
     */
    class Parameter : public Functional {
    public:
      static const VtableType vtable;

      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index);
      template<typename V> static void visit(V& v);
      static TermResultType check_type_impl(const Parameter& self);

      /// Type of this parameter
      TreePtr<Term> parameter_type;
      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned depth;
      /// Index of this parameter in its scope.
      unsigned index;
    };

    TreePtr<Term> functional_unwrap(const TreePtr<Term>& term);
  }
}

#endif
