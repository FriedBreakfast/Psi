#ifndef HPP_PSI_COMPILER_TERM
#define HPP_PSI_COMPILER_TERM

#include "TreeBase.hpp"
#include "Enums.hpp"

#include <boost/intrusive/unordered_set.hpp>

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
    
    class TermBinaryRewriter;    
    
    struct TermBinaryRewriterVtable {
      SIVtable base;
      PsiBool (*binary_rewrite) (TermBinaryRewriter*, TreePtr<Term>*, const TreePtr<Term>*, const SourceLocation*);
    };
    
    class TermBinaryRewriter : public SIBase {
    public:
      static const SIVtable vtable;
      typedef TermBinaryRewriterVtable VtableType;
      
      TermBinaryRewriter(const VtableType *vptr) {PSI_COMPILER_SI_INIT(vptr);}
      
      /**
       * \brief Rewrite two terms into one.
       * 
       * The result is placed into \c lhs. The return value indicates whether
       * the operation was successful.
       */
      bool binary_rewrite(TreePtr<Term>& lhs, const TreePtr<Term>& rhs, const SourceLocation& location) {
        return derived_vptr(this)->binary_rewrite(this, &lhs, &rhs, &location);
      }
    };
    
    template<typename Derived>
    struct TermBinaryRewriterWrapper : NonConstructible {
      static PsiBool rewrite(TermBinaryRewriter *self, TreePtr<Term> *lhs, const TreePtr<Term> *rhs, const SourceLocation *location) {
        return Derived::binary_rewrite_impl(*static_cast<Derived*>(self), *lhs, *rhs, *location);
      }
    };

#define PSI_COMPILER_TERM_BINARY_REWRITER(cls,name,base) { \
    PSI_COMPILER_SI(name,&base::vtable), \
    &::Psi::Compiler::TermBinaryRewriterWrapper<cls>::rewrite \
  }
    
    class TermVisitorVisitor : public ObjectVisitorBase<TermVisitorVisitor> {
      TermVisitor *m_v;

      template<typename T> void visit_tree_ptr_helper(const TreePtr<T>&, Tree*) {}
      template<typename T> void visit_tree_ptr_helper(const TreePtr<T>& ptr, Term*) {m_v->visit(ptr);}
      
    public:
      TermVisitorVisitor(TermVisitor *v) : m_v(v) {}
      template<typename T> void visit_object_ptr(const ObjectPtr<T>&) {}
      template<typename T> void visit_tree_ptr(const TreePtr<T>& ptr) {visit_tree_ptr_helper(ptr, static_cast<T*>(NULL));}
      template<typename T, typename U> void visit_delayed(const DelayedValue<T,U>& ptr) {
        boost::array<T*, 1> star = {{const_cast<T*>(&ptr.get_checked())}};
        visit_callback(*this, NULL, star);
      }
      template<typename T> boost::true_type do_visit_base(VisitorTag<T>) {return boost::true_type();}
      boost::false_type do_visit_base(VisitorTag<Object>) {return boost::false_type();}
    };
    
    struct TermResultInfo {
      /// Term type
      TreePtr<Term> type;
      /// Result storage mode
      TermMode mode;
      /// Whether different occurrences of the term are equivalent
      PsiBool pure;
      
      TermResultInfo() {}
      TermResultInfo(const TreePtr<Term>& type_, TermMode mode_, bool pure_) : type(type_), mode(mode_), pure(pure_) {}

      template<typename V>
      static void visit(V& v) {
        v("type", &TermResultInfo::type)
        ("mode", &TermResultInfo::mode)
        ("pure", &TermResultInfo::pure);
      }
    };
    
    struct TermTypeInfo {
      /// Whether terms of this type have fixed size
      PsiBool type_fixed_size;
      /// What sort of type this is; if it is a type.
      TypeMode type_mode;
      
      template<typename V>
      static void visit(V& v) {
        v("type_fixed_size", &TermTypeInfo::type_fixed_size)
        ("type_mode", &TermTypeInfo::type_mode);
      }
    };
    
    struct TermVtable {
      TreeVtable base;
      void (*visit) (const Term*, TermVisitor*);
      void (*type_info) (TermTypeInfo*, const Term*);
    };

    class Term : public Tree {
      friend class Functional;
      Term(const TermVtable *vptr);
      friend class Statement;
      Term(const TermVtable *vptr, CompileContext& compile_context, const TermResultInfo& type, const SourceLocation& location);
      
      void type_info_compute() const;

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;

      PSI_COMPILER_EXPORT static const SIVtable vtable;
      Term(const TermVtable *vtable, const TermResultInfo& type, const SourceLocation& location);

      /// \brief The type of this term.
      TreePtr<Term> type;
      /// \brief Result mode of this term
      TermMode mode;
      /// \brief Whether this term is pure, i.e. different occurences of the same tree are type equivalent
      PsiBool pure;
    private:
      mutable PsiBool m_type_info_computed;
      mutable TermTypeInfo m_type_info;
    public:
      
      /**
       * \brief Get the result information of this term as a TermResultInfo structure
       * 
       * This just collects the fields \c type, \c mode and \c pure
       */
      TermResultInfo result_info() const {
        return TermResultInfo(type, mode, pure);
      }
      
      /**
       * \brief Get (lazily computed) information about this terms result.
       */
      const TermTypeInfo& type_info() const {
        if (!m_type_info_computed)
          type_info_compute();
        return m_type_info;
      }
      
      /**
       * \brief Is this a functional value?
       * 
       * This is true when the result of this term is not a reference and
       * its type is can be stored in a register.
       */
      bool is_functional() const {
        return (mode == term_mode_value) && (!type || type->is_register_type());
      }

      /**
       * \brief Is this a type?
       * 
       * This means "can this be the type of another term". Therefore, Metatype
       * counts as a type here.
       */
      bool is_type() const {return !type || !type->type;}
      
      /**
       * \brief Is this a primitive type?
       */
      bool is_primitive_type() const {
        const TermTypeInfo& tri = type_info();
        return (tri.type_mode == type_mode_metatype) || (tri.type_mode == type_mode_primitive);
      }
      
      /// \brief Can this type be stored in a register?
      bool is_register_type() const {
        const TermTypeInfo& tri = type_info();
        return ((tri.type_mode == type_mode_metatype) || (tri.type_mode == type_mode_primitive)) && tri.type_fixed_size;
      }
      
      PSI_COMPILER_EXPORT bool unify(TreePtr<Term>& other, const SourceLocation& location) const;
      
      enum UprefMatchMode {
        upref_match_read,
        upref_match_write,
        upref_match_exact,
        upref_match_ignore
      };
      
      PSI_COMPILER_EXPORT bool match(const TreePtr<Term>& value, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth, UprefMatchMode upref_mode) const;
      PSI_COMPILER_EXPORT bool convert_match(const TreePtr<Term>& value) const;
      PSI_COMPILER_EXPORT TreePtr<Term> parameterize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Anonymous> >& elements) const;
      PSI_COMPILER_EXPORT TreePtr<Term> specialize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values) const;
      PSI_COMPILER_EXPORT TreePtr<Term> anonymize(const SourceLocation& location, const PSI_STD::vector<TreePtr<Statement> >& statements) const;
      PSI_COMPILER_EXPORT TreePtr<Term> anonymize(const SourceLocation& location) const;

      /**
       * Visit all terms referenced by this term.
       */
      void visit_terms(TermVisitor& visitor) const {
        derived_vptr(this)->visit(this, &visitor);
      }

      template<typename Visitor> static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("type", &Term::type)
        ("pure", &Term::pure)
        ("mode", &Term::mode)
        ("type_info_computed", &Term::m_type_info_computed)
        ("type_info", &Term::m_type_info);
      }
      
      template<typename Derived>
      static void visit_terms_impl(const Derived& self, TermVisitor& v) {
        Derived::local_complete_impl(self);
        boost::array<Derived*, 1> ptrs = {{const_cast<Derived*>(&self)}};
        TermVisitorVisitor vv(&v);
        visit_members(vv, ptrs);
      }
      
      static TermTypeInfo type_info_impl(const Term& self);
    };

    template<typename Derived>
    struct TermWrapper : NonConstructible {
      static void visit(const Term *self, TermVisitor *visitor) {
        Derived::visit_terms_impl(*static_cast<const Derived*>(self), *visitor);
      }
      
      static void type_info(TermTypeInfo *out, const Term *self) {
        new (out) TermTypeInfo (Derived::type_info_impl(*static_cast<const Derived*>(self)));
      }
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TermWrapper<derived>::visit, \
    &::Psi::Compiler::TermWrapper<derived>::type_info, \
  }

    class TermRewriterVisitor : public ObjectVisitorBase<TermRewriterVisitor> {
      TermRewriter *m_rw;
      
    public:
      TermRewriterVisitor(TermRewriter *rw) : m_rw(rw) {}
      
      template<typename T> void visit_tree_ptr_helper(TreePtr<T>&, Tree*) {}
      
      template<typename T>
      void visit_tree_ptr_helper(TreePtr<T>& ptr, Term*) {
        if (ptr)
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

      template<typename T> boost::true_type do_visit_base(VisitorTag<T>) {return boost::true_type();}
      boost::false_type do_visit_base(VisitorTag<Term>) {return boost::false_type();}
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

    template<typename Derived>
    class TermBinaryVisitBase : boost::noncopyable {
      Derived& derived() {return *static_cast<Derived*>(this);}
      
    public:
      bool result;

      TermBinaryVisitBase() : result(true) {}

      void visit_base(const boost::array<Term*,2>&) {}

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
      
    private:
      template<typename T>
      bool treeptr_compare(TreePtr<T>& lhs, TreePtr<T>& rhs, Tree*) {
        return lhs == rhs;
      }

      template<typename T>
      bool treeptr_compare(TreePtr<T>& lhs, TreePtr<T>& rhs, Term*) {
        return derived().term_visit(lhs, rhs);
      }

    public:
      template<typename T>
      void visit_object(const char*, const boost::array<TreePtr<T>*, 2>& ptr) {
        if (!result)
          return;
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

    /**
     * Term visitor to comparison.
     */
    class TermBinaryRewriterVisitor : public TermBinaryVisitBase<TermBinaryRewriterVisitor> {
      TermBinaryRewriter *m_v;
      const SourceLocation *m_location;

    public:
      TermBinaryRewriterVisitor(TermBinaryRewriter *v, const SourceLocation *location) : m_v(v), m_location(location) {}
      
      bool term_visit(TreePtr<Term>& lhs, const TreePtr<Term>& rhs) {
        return m_v->binary_rewrite(lhs, rhs, *m_location);
      }

      template<typename T>
      bool term_visit(TreePtr<T>& lhs, const TreePtr<T>& rhs) {
        TreePtr<Term> tmp = lhs;
        if (m_v->binary_rewrite(tmp, rhs, *m_location)) {
          lhs = treeptr_cast<T>(tmp);
          return true;
        } else {
          return false;
        }
      }
    };

    /**
     * Term visitor to comparison.
     */
    class TermComparatorVisitor : public TermBinaryVisitBase<TermComparatorVisitor> {
      TermComparator *m_v;

    public:
      TermComparatorVisitor(TermComparator *v) : m_v(v) {}
      
      template<typename T>
      bool term_visit(const TreePtr<T>& lhs, const TreePtr<T>& rhs) {
        return m_v->compare(lhs, rhs);
      }
    };
    
    class Functional;
    
    struct FunctionalVtable {
      TermVtable base;
      void (*simplify) (TreePtr<Term>*, const Functional*);
      std::size_t (*hash) (const Functional*);
      PsiBool (*equivalent) (const Functional*, const Functional*);
      void (*check_type) (TermResultInfo*, const Functional*);
      Functional* (*clone) (const Functional*);
      void (*rewrite) (TreePtr<Term>*, const Functional*,TermRewriter*,const SourceLocation*);
      PsiBool (*binary_rewrite) (TreePtr<Term>*,const Functional*,const Functional*,TermBinaryRewriter*,const SourceLocation*);
      PsiBool (*compare) (const Functional*,const Functional*,TermComparator*);
    };
    
    /**
     * \brief Base class for (most) functional values.
     * 
     * Apart from built-in function calls, all terms which only take pure
     * functional arguments derive from this.
     */
    class Functional : public Term {
      friend class CompileContext;
      std::size_t m_hash;
      typedef boost::intrusive::unordered_set_member_hook<> TermSetHook;
      TermSetHook m_set_hook;

    public:
      typedef FunctionalVtable VtableType;
      PSI_COMPILER_EXPORT static const SIVtable vtable;
      Functional(const VtableType *vptr);
      ~Functional();
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Term>(v);
      }
      
      
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
      
      bool binary_rewrite(TreePtr<Term>& output, const Functional& other, TermBinaryRewriter& rewriter, const SourceLocation& location) const {
        return derived_vptr(this)->binary_rewrite(&output, this, &other, &rewriter, &location);
      }
      
      bool compare(const Functional& other, TermComparator& cmp) const {
        PSI_ASSERT(si_vptr(this) == si_vptr(&other));
        return derived_vptr(this)->compare(this, &other, &cmp);
      }
      
      TermResultInfo check_type() const {
        ResultStorage<TermResultInfo> result;
        derived_vptr(this)->check_type(result.ptr(), this);
        return result.done();
      }
      
      Functional* clone() const {
        return derived_vptr(this)->clone(this);
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
      static bool binary_rewrite_impl(TreePtr<Term>& output, const Derived& lhs, const Derived& rhs, TermBinaryRewriter& rewriter, const SourceLocation& location) {
        TermBinaryRewriterVisitor rw(&rewriter, &location);
        Derived copy(lhs);
        boost::array<Derived*, 2> ptr = {{&copy, const_cast<Derived*>(&rhs)}};
        visit_members(rw, ptr);
        if (rw.result) {
          output = lhs.compile_context().get_functional(copy, location);
          return true;
        } else {
          return false;
        }
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
      
      static void check_type(TermResultInfo *out, const Functional *self) {
        new (out) TermResultInfo (Derived::check_type_impl(*static_cast<const Derived*>(self)));
      }
      
      static Functional* clone(const Functional *self) {
        return ::new Derived(*static_cast<const Derived*>(self));
      }
      
      static void rewrite(TreePtr<Term> *out, const Functional *self, TermRewriter *cmp, const SourceLocation *location) {
        new (out) TreePtr<Term> (Derived::rewrite_impl(*static_cast<const Derived*>(self), *cmp, *location));
      }
      
      static PsiBool binary_rewrite(TreePtr<Term> *out, const Functional *lhs, const Functional *rhs, TermBinaryRewriter *cmp, const SourceLocation *location) {
        return Derived::binary_rewrite_impl(*out, *static_cast<const Derived*>(lhs), *static_cast<const Derived*>(rhs), *cmp, *location);
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
    &::Psi::Compiler::FunctionalWrapper<derived>::clone, \
    &::Psi::Compiler::FunctionalWrapper<derived>::rewrite, \
    &::Psi::Compiler::FunctionalWrapper<derived>::binary_rewrite, \
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
      template<typename Visitor> static void visit(Visitor& v) {visit_base<Functional>(v);}
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_FUNCTIONAL(derived,name,super)

    /**
     * \brief Type of types.
     */
    class Metatype : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      Metatype();
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const Metatype& self);
      static TermTypeInfo type_info_impl(const Metatype& self);
    };

    /**
     * Anonymous term. Has a type but no defined value.
     *
     * The value must be defined elsewhere, for example by being part of a function.
     */
    class Anonymous : public Term {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

      Anonymous(const TreePtr<Term>& type, TermMode mode, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const Anonymous& self);
      TermMode mode;
    };

    /**
     * \brief Parameter to a Pattern.
     */
    class Parameter : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const Parameter& self);
      static TermTypeInfo type_info_impl(const Parameter& self);

      /// Type of this parameter
      TreePtr<Term> parameter_type;
      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned depth;
      /// Index of this parameter in its scope.
      unsigned index;
    };

    TreePtr<Term> term_unwrap(const TreePtr<Term>& term);
    
    /**
     * \brief Try to unwrap a term and cast it to another term type.
     * 
     * This uses \c type_unwrap to unwrap the term.
     */
    template<typename T> TreePtr<T> term_unwrap_dyn_cast(const TreePtr<Term>& term) {return dyn_treeptr_cast<T>(term_unwrap(term));}
    
    /**
     * \brief Unwrap a term and cast it to another term type.
     */
    template<typename T> TreePtr<T> term_unwrap_cast(const TreePtr<Term>& term) {return treeptr_cast<T>(term_unwrap(term));}

    /**
     * \brief Try to unwrap a term to a type.
     */
    template<typename T> bool term_unwrap_isa(const TreePtr<Term>& term) {return tree_isa<T>(term_unwrap(term));}
  }
}

#endif
