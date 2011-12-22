#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <boost/array.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/avl_set.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits/is_convertible.hpp>

#include "CppCompiler.hpp"
#include "GarbageCollection.hpp"
#include "Runtime.hpp"
#include "Visitor.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Parser {
    struct Expression;
    struct NamedExpression;
  }
  
  namespace Compiler {
    class CompileException : public std::exception {
    public:
      CompileException();
      virtual ~CompileException() throw();
      virtual const char *what() const throw();
    };

    struct SourceFile {
      String url;
    };

    struct PhysicalSourceLocation {
      SharedPtr<SourceFile> file;
      int first_line;
      int first_column;
      int last_line;
      int last_column;
    };

    class LogicalSourceLocation;
    typedef IntrusivePointer<LogicalSourceLocation> LogicalSourceLocationPtr;

    class LogicalSourceLocation : public boost::intrusive::avl_set_base_hook<> {
      struct Key {
	      unsigned index;
	      String name;

	      bool operator < (const Key&) const;
      };

      struct Compare {bool operator () (const LogicalSourceLocation&, const LogicalSourceLocation&) const;};
      struct KeyCompare;

      std::size_t m_reference_count;
      Key m_key;
      LogicalSourceLocationPtr m_parent;
      typedef boost::intrusive::avl_set<LogicalSourceLocation, boost::intrusive::constant_time_size<false>, boost::intrusive::compare<Compare> > ChildMapType;
      ChildMapType m_children;

      LogicalSourceLocation(const Key&, const LogicalSourceLocationPtr&);

    public:
      ~LogicalSourceLocation();

      /// \brief Whether this location is anonymous within its parent.
      bool anonymous() {return m_parent && (m_key.index != 0);}
      /// \brief The identifying index of this location if it is anonymous.
      unsigned index() {return m_key.index;}
      /// \brief The name of this location within its parent if it is not anonymous.
      const String& name() {return m_key.name;}
      /// \brief Get the parent node of this location
      const LogicalSourceLocationPtr& parent() {return m_parent;}

      unsigned depth();
      LogicalSourceLocationPtr ancestor(unsigned depth);
      String error_name(const LogicalSourceLocationPtr& relative_to, bool ignore_anonymous_tail=false);
#if defined(PSI_DEBUG) || defined(PSI_DOXYGEN)
      void dump_error_name();
#endif

      static LogicalSourceLocationPtr new_root_location();
      LogicalSourceLocationPtr named_child(const String& name);
      LogicalSourceLocationPtr new_anonymous_child();

      friend void intrusive_ptr_add_ref(LogicalSourceLocation *self) {
        ++self->m_reference_count;
      }

      friend void intrusive_ptr_release(LogicalSourceLocation *self) {
        if (!--self->m_reference_count)
          delete self;
      }
    };

    struct SourceLocation {
      PhysicalSourceLocation physical;
      LogicalSourceLocationPtr logical;

      SourceLocation(const PhysicalSourceLocation& physical_,  const LogicalSourceLocationPtr& logical_)
      : physical(physical_), logical(logical_) {}

      SourceLocation relocate(const PhysicalSourceLocation& new_physical) const {
        return SourceLocation(new_physical, logical);
      }

      SourceLocation named_child(const String& name) const {
        return SourceLocation(physical, logical->named_child(name));
      }
    };

    /**
     * \brief Single inheritance dispatch table base.
     */
    struct SIVtable {
      const SIVtable *super;
      const char *classname;
      bool abstract;
    };

#define PSI_COMPILER_SI(classname,super) {reinterpret_cast<const SIVtable*>(super),classname,false}
#define PSI_COMPILER_SI_ABSTRACT(classname,super) {reinterpret_cast<const SIVtable*>(super),classname,true}
#define PSI_COMPILER_SI_INIT(vptr) (m_vptr = reinterpret_cast<const SIVtable*>(vptr), PSI_ASSERT(!m_vptr->abstract))

    /**
     * \brief Single inheritance base.
     */
    class SIBase {
      friend bool si_is_a(const SIBase*, const SIVtable*);
      friend const SIVtable* si_vptr(const SIBase*);

    protected:
      const SIVtable *m_vptr;
    };

    inline const SIVtable* si_vptr(const SIBase *self) {return self->m_vptr;}
    bool si_is_a(const SIBase*, const SIVtable*);

    template<typename T>
    const typename T::VtableType* derived_vptr(T *ptr) {
      return reinterpret_cast<const typename T::VtableType*>(si_vptr(ptr));
    }

    class TreeBase;
    class Tree;
    class Type;
    class CompileContext;

    class TreePtrBase {
      typedef void (TreePtrBase::*safe_bool_type) () const;
      void safe_bool_true() const {}

      mutable const TreeBase *m_ptr;

      void initialize(const TreeBase *ptr, bool add_ref);
      const Tree* get_helper() const;
      void update_chain(const TreeBase *ptr) const;

    protected:
      void swap(TreePtrBase& other) {std::swap(m_ptr, other.m_ptr);}
      
    public:
      ~TreePtrBase();
      TreePtrBase() {}
      explicit TreePtrBase(const TreeBase *ptr, bool add_ref) {initialize(ptr, add_ref);}
      TreePtrBase(const TreePtrBase& src) {initialize(src.m_ptr, true);}
      TreePtrBase& operator = (const TreePtrBase& src) {TreePtrBase(src).swap(*this); return *this;}
      
      const Tree* get() const;
      const TreeBase* raw_get() const {return m_ptr;}
      const TreeBase* release() {const TreeBase *tmp = m_ptr; m_ptr = NULL; return tmp;}

      operator safe_bool_type () const {return get() ? &TreePtrBase::safe_bool_true : 0;}
      bool operator ! () const {return !get();}
      template<typename U> bool operator == (const TreePtrBase& other) const {return get() == other.get();};
      template<typename U> bool operator != (const TreePtrBase& other) const {return get() != other.get();};
      template<typename U> bool operator < (const TreePtrBase& other) const {return get() < other.get();};

      CompileContext& compile_context() const;
      const SourceLocation& location() const;
    };

    template<typename T=Tree>
    class TreePtr : public TreePtrBase {
      template<typename U> friend TreePtr<U> tree_from_base(const TreeBase*, bool);
      TreePtr(const TreeBase *src, bool add_ref) : TreePtrBase(src, add_ref) {}

    public:
      TreePtr() {}
      explicit TreePtr(const T *ptr, bool add_ref=true) : TreePtrBase(ptr, add_ref) {}
      template<typename U> TreePtr(const TreePtr<U>& src) : TreePtrBase(src) {BOOST_STATIC_ASSERT((boost::is_convertible<U*, T*>::value));}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {TreePtr<T>(src).swap(*this); return *this;}

      const T* get() const;
      const T* operator -> () const {return get();}

      void reset(const T *ptr=NULL, bool add_ref=true) {TreePtr<T>(ptr, add_ref).swap(*this);}
      void swap(TreePtr<T>& other) {TreePtrBase::swap(other);}
    };

    /**
     * Get a TreePtr from a point to a TreeBase.
     *
     * This should only be used in wrapper functions, since otherwise the type of \c base
     * should be statically known.
     */
    template<typename T>
    TreePtr<T> tree_from_base(const TreeBase *base, bool add_ref=true) {
      return TreePtr<T>(base, add_ref);
    }

    class VisitorPlaceholder {
    public:
      template<typename T>
      VisitorPlaceholder& operator () (const char *name PSI_ATTRIBUTE((PSI_UNUSED)), T& member PSI_ATTRIBUTE((PSI_UNUSED))) {
        return *this;
      }
    };

    class Tree;
    class TreeBase;
    class TreeCallback;
    class Term;

    /// \see TreeBase
    struct TreeBaseVtable {
      SIVtable base;
      void (*destroy) (TreeBase*);
      void (*gc_increment) (TreeBase*);
      void (*gc_decrement) (TreeBase*);
      void (*gc_clear) (TreeBase*);
      bool is_callback;
    };

    /**
     * Extends SIBase for lazy evaluation of Trees.
     *
     * Two types derive from this: Tree, which holds values, and TreeCallbackHolder, which
     * encapsulates a callback to return a Tree.
     */
    class TreeBase : public SIBase, public boost::intrusive::list_base_hook<> {
      friend class CompileContext;
      friend class RunningTreeCallback;
      friend class TreeCallback;
      friend class GCVisitorIncrement;
      friend class GCVisitorDecrement;
      friend class GCVisitorClear;
      friend class TreePtrBase;

      mutable std::size_t m_reference_count;
      CompileContext *m_compile_context;
      SourceLocation m_location;

    protected:
      CompileContext& compile_context() const {return *m_compile_context;}
      const SourceLocation& location() const {return m_location;}

    public:
      typedef TreeBaseVtable VtableType;
      static const SIVtable vtable;
      
      TreeBase(CompileContext& compile_context, const SourceLocation& location);
      ~TreeBase();

      template<typename Visitor>
      static void visit(Visitor& visitor PSI_ATTRIBUTE((PSI_UNUSED))) {}
    };

    inline void TreePtrBase::initialize(const TreeBase *ptr, bool add_ref) {
      m_ptr = ptr;
      if (add_ref && m_ptr)
        ++m_ptr->m_reference_count;
    }

    inline TreePtrBase::~TreePtrBase() {
      if (m_ptr)
        if (!--m_ptr->m_reference_count)
          derived_vptr(m_ptr)->destroy(const_cast<TreeBase*>(m_ptr));
    }

    /// \brief Get the compile context for this Tree, without evaluating the Tree.
    inline CompileContext& TreePtrBase::compile_context() const {return *m_ptr->m_compile_context;}
    /// \brief Get the location of this Tree, without evaluating the Tree.
    inline const SourceLocation& TreePtrBase::location() const {return m_ptr->m_location;}

    /**
     * \brief Base classes for gargabe collection phase
     * implementations.
     */
    template<typename Derived>
    class TreeVisitorBase {
      Derived& derived() {
        return *static_cast<Derived*>(this);
      }

    public:
      /// Simple types cannot hold references, so we aren't interested in them.
      template<typename T>
      void visit_simple(const char*, const boost::array<T*, 1>&) {
      }
      
      template<typename T>
      void visit_object(const char*, const boost::array<T*,1>& obj) {
        visit_members(*this, obj);
      }

      /// Simple pointers are assumed to be owned by this object
      template<typename T>
      void visit_object(const char*, const boost::array<T**,1>& obj) {
        if (obj[0]) {
          boost::array<T*, 1> star = {{*obj[0]}};
          visit_callback(*this, NULL, star);
        }
      }

      /// Shared pointers cannot reference trees (this would break the GC), so they are ignored.
      template<typename T>
      void visit_object(const char*, const boost::array<SharedPtr<T>*,1>&) {
      }

      template<typename T>
      void visit_object(const char*, const boost::array<TreePtr<T>*, 1>& ptr) {
        derived().visit_tree_ptr(*ptr[0]);
      }
      
      template<typename T>
      void visit_sequence (const char*, const boost::array<T*,1>& collections) {
        for (typename T::iterator ii = collections[0]->begin(), ie = collections[0]->end(); ii != ie; ++ii) {
          boost::array<typename T::value_type*, 1> m = {{&*ii}};
          visit_callback(*this, NULL, m);
        }
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*,1>& maps) {
        for (typename T::iterator ii = maps[0]->begin(), ie = maps[0]->end(); ii != ie; ++ii) {
#if 0
          boost::array<const typename T::key_type*, 1> k = {{&ii->first}};
          visit_object(NULL, k);
#endif
          boost::array<typename T::mapped_type*, 1> v = {{&ii->second}};
          visit_callback(*this, NULL, v);
        }
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorIncrement : public TreeVisitorBase<GCVisitorIncrement> {
    public:
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        if (ptr)
          ++ptr.raw_get()->m_reference_count;
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorDecrement : public TreeVisitorBase<GCVisitorDecrement> {
    public:
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        if (ptr)
          --ptr.raw_get()->m_reference_count;
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorClear : public TreeVisitorBase<GCVisitorClear> {
    public:
      template<typename T>
      void visit_sequence(const char*, const boost::array<T*,1>& seq) {
        seq[0]->clear();
      }

      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        ptr.reset();
      }
    };

    template<typename Derived>
    struct TreeBaseWrapper : NonConstructible {
      static void destroy(TreeBase *self) {
        delete static_cast<Derived*>(self);
      }

      static void gc_increment(TreeBase *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorIncrement p;
        visit_members(p, a);
      }

      static void gc_decrement(TreeBase *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorDecrement p;
        visit_members(p, a);
      }

      static void gc_clear(TreeBase *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorClear p;
        visit_members(p, a);
      }
    };

#define PSI_COMPILER_TREE_BASE(is_callback,derived,name,super) { \
    PSI_COMPILER_SI(name,&super::vtable), \
    &::Psi::Compiler::TreeBaseWrapper<derived>::destroy, \
    &::Psi::Compiler::TreeBaseWrapper<derived>::gc_increment, \
    &::Psi::Compiler::TreeBaseWrapper<derived>::gc_decrement, \
    &::Psi::Compiler::TreeBaseWrapper<derived>::gc_clear, \
    (is_callback) \
  }

    /// \see Tree
    struct TreeVtable {
      TreeBaseVtable base;
      void (*complete) (Tree*);
      PsiBool (*match) (const Tree*,const Tree*,const void*,void*,unsigned);
    };

    class Tree : public TreeBase {
    public:
      static const SIVtable vtable;
      static const bool match_unique = true;
      
      typedef TreeVtable VtableType;

      Tree(CompileContext&, const SourceLocation&);

      /**
       * Recursively evaluate all tree references inside this tree.
       */
      void complete() const {derived_vptr(this)->complete(const_cast<Tree*>(this));}

      /**
       * Check whether this term matches another term.
       */
      bool match(const TreePtr<Tree>& value, const List<TreePtr<Term> >& wildcards, unsigned depth=0) const;
    };

    template<typename T> const T* TreePtr<T>::get() const {
      const Tree *t = TreePtrBase::get();
      PSI_ASSERT(!t || si_is_a(t, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<const T*>(t);
    };

    template<typename T>
    const T* tree_cast(const Tree *ptr) {
      PSI_ASSERT(si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<const T*>(ptr);
    }

    template<typename T>
    const T* dyn_tree_cast(const Tree *ptr) {
      return si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable)) ? static_cast<const T*>(ptr) : 0;
    }

    template<typename T, typename U>
    TreePtr<T> treeptr_cast(const TreePtr<U>& ptr) {
      return TreePtr<T>(tree_cast<T>(ptr.get()));
    }

    template<typename T, typename U>
    TreePtr<T> dyn_treeptr_cast(const TreePtr<U>& ptr) {
      return TreePtr<T>(dyn_tree_cast<T>(ptr.get()));
    }

    /**
     * \brief Recursively completes a tree.
     */
    class CompleteVisitor : public TreeVisitorBase<CompleteVisitor> {
    public:
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        if (ptr)
          ptr->complete();
      }
    };

    /**
     * Term visitor to perform pattern matching.
     */
    class MatchVisitor {
      List<TreePtr<Term> > m_wildcards;
      unsigned m_depth;

    public:
      bool result;

      MatchVisitor(const List<TreePtr<Term> >& wildcards, unsigned depth)
      : m_wildcards(wildcards), m_depth(depth), result(true) {}

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

      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        if (!result)
          return;

        result = (*ptr[0])->match(*ptr[1], m_wildcards, m_depth);
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

    template<typename Derived>
    struct TreeWrapper : NonConstructible {
      static void complete(Tree *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        CompleteVisitor p;
        visit_members(p, a);
      }

      static PsiBool match(const Tree *left, const Tree *right, const void *wildcards_vtable, void *wildcards_obj, unsigned depth) {
        if (Derived::match_unique) {
          return left == right;
        } else {
          boost::array<const Derived*, 2> pair = {{static_cast<const Derived*>(left), static_cast<const Derived*>(right)}};
          MatchVisitor mv(List<TreePtr<Term> >(wildcards_vtable, wildcards_obj), depth);
          visit_members(mv, pair);
          return mv.result;
        }
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_TREE_BASE(false,derived,name,super), \
    &::Psi::Compiler::TreeWrapper<derived>::complete, \
    &::Psi::Compiler::TreeWrapper<derived>::match \
  }

#define PSI_COMPILER_TREE_INIT() (PSI_REQUIRE_CONVERTIBLE(&vtable, const VtableType*), PSI_COMPILER_SI_INIT(&vtable))
#define PSI_COMPILER_TREE_ABSTRACT(name,super) PSI_COMPILER_SI_ABSTRACT(name,&super::vtable)

    inline const Tree* TreePtrBase::get() const {
      return (!m_ptr || !derived_vptr(m_ptr)->is_callback) ? static_cast<const Tree*>(m_ptr) : get_helper();
    }

    /// \see TreeCallback
    struct TreeCallbackVtable {
      TreeBaseVtable base;
      const TreeBase* (*evaluate) (TreeCallback*);
    };

    class TreeCallback : public TreeBase {
      friend class TreePtrBase;
      friend class RunningTreeCallback;

    public:
      enum CallbackState {
        state_ready,
        state_running,
        state_finished,
        state_failed
      };

    private:
      CallbackState m_state;
      TreePtr<> m_value;

    protected:
      CompileContext& compile_context() const {return *m_compile_context;}
      const SourceLocation& location() const {return m_location;}

    public:
      static const SIVtable vtable;

      TreeCallback(CompileContext&, const SourceLocation&);

      template<typename Visitor> static void visit(Visitor& v) {
        visit_base<TreeBase>(v);
        v("value", &TreeCallback::m_value);
      }
    };

    /**
     * \brief Data for a running TreeCallback.
     */
    class RunningTreeCallback : public boost::noncopyable {
      TreeCallback *m_callback;
      RunningTreeCallback *m_parent;

    public:
      RunningTreeCallback(TreeCallback *callback);
      ~RunningTreeCallback();

      static void throw_circular_dependency(TreeCallback *callback) PSI_ATTRIBUTE((PSI_NORETURN));
    };

    template<typename Derived>
    struct TreeCallbackWrapper : NonConstructible {
      static const TreeBase* evaluate(TreeCallback *self) {
        return Derived::evaluate_impl(*static_cast<Derived*>(self)).release();
      }
    };

#define PSI_COMPILER_TREE_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE_BASE(true,derived,name,super), \
    &::Psi::Compiler::TreeCallbackWrapper<derived>::evaluate \
  }

#define PSI_COMPILER_TREE_CALLBACK_INIT() PSI_COMPILER_SI_INIT(&vtable)

    /// To get past preprocessor's inability to understand template parameters
    template<typename T, typename Functor>
    struct TreeCallbackImplArgs {
      typedef T TreeType;
      typedef Functor FunctionType;
    };

    template<typename Args>
    class TreeCallbackImpl : public TreeCallback {
    public:
      typedef typename Args::TreeType TreeType;
      typedef typename Args::FunctionType FunctionType;

    private:
      FunctionType *m_function;

    public:
      static const TreeCallbackVtable vtable;

      TreeCallbackImpl(CompileContext& compile_context, const SourceLocation& location, const FunctionType& function)
      : TreeCallback(compile_context, location), m_function(new FunctionType(function)) {
        PSI_COMPILER_TREE_CALLBACK_INIT();
      }

      ~TreeCallbackImpl() {
        if (m_function)
          delete m_function;
      }

      static TreePtr<TreeType> evaluate_impl(TreeCallbackImpl& self) {
        PSI_ASSERT(self.m_function);
        boost::scoped_ptr<FunctionType> function_copy(self.m_function);
        self.m_function = NULL;
        return function_copy->evaluate(tree_from_base<TreeType>(&self));
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<TreeCallback>(v);
        v("function", &TreeCallbackImpl::m_function);
      }
    };

    template<typename T>
    const TreeCallbackVtable TreeCallbackImpl<T>::vtable = PSI_COMPILER_TREE_CALLBACK(TreeCallbackImpl<T>, "(anonymous)", TreeCallback);

    /**
     * \brief Make a lazily evaluated Tree from a C++ functor object.
     */
    template<typename T, typename Callback>
    TreePtr<T> tree_callback(CompileContext& compile_context, const SourceLocation& location, const Callback& callback) {
      return tree_from_base<T>(new TreeCallbackImpl<TreeCallbackImplArgs<T, Callback> >(compile_context, location, callback));
    }

    class Anonymous;
    class Term;
    class Interface;

    struct TermVtable {
      TreeVtable base;
      const TreeBase* (*parameterize) (const Term*, const SourceLocation*, const void*, void*, unsigned);
      const TreeBase* (*specialize) (const Term*, const SourceLocation*, const void*, void*, unsigned);
      const TreeBase* (*interface_search) (const Term*, const TreeBase*, const void*, void*);
    };

    class Term : public Tree {
      friend class Metatype;
      Term(CompileContext&, const SourceLocation&);

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;

      static const SIVtable vtable;

      Term(const TreePtr<Term>&, const SourceLocation&);

      /// \brief The type of this term.
      TreePtr<Term> type;

      bool is_type() const;


      /// \brief Replace anonymous terms in the list by parameters
      TreePtr<Term> parameterize(const SourceLocation& location, const List<TreePtr<Anonymous> >& elements, unsigned depth) const {
        return tree_from_base<Term>(derived_vptr(this)->parameterize(this, &location, elements.vptr(), elements.object(), depth), false);
      }

      /// \brief Replace parameter terms in this tree by given values
      TreePtr<Term> specialize(const SourceLocation& location, const List<TreePtr<Term> >& values, unsigned depth) const {
        return tree_from_base<Term>(derived_vptr(this)->specialize(this, &location, values.vptr(), values.object(), depth), false);
      }

      TreePtr<> interface_search(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) const {
        return tree_from_base<Tree>(derived_vptr(this)->interface_search(this, interface.raw_get(), parameters.vptr(), parameters.object()), false);
      }

      template<typename Visitor> static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("type", &Term::type);
      }

      static TreePtr<> interface_search_impl(const Term& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
    };

    template<typename Derived>
    class RewriteVisitorBase {
      bool m_changed;

    public:
      RewriteVisitorBase() : m_changed(false) {}

      bool changed() const {return m_changed;}

      template<typename T>
      void visit_simple(const char*, const boost::array<T*, 2>& obj) {
        *obj[0] = *obj[1];
      }

      template<typename T>
      void visit_object(const char*, const boost::array<T*, 2>& obj) {
        visit_members(*this, obj);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<const TreePtr<T>*, 2>& ptr) {
        *ptr[0] = static_cast<Derived*>(this)->visit_tree_ptr(*ptr[1]);
        if (*ptr[0] != *ptr[1])
          m_changed = true;
      }

      template<typename T>
      void visit_collection(const char*, const boost::array<T*,2>& collections) {
        for (typename T::iterator ii = collections[1]->begin(), ie = collections[1]->end(); ii != ie; ++ii) {
          typename T::value_type vt;
          boost::array<typename T::pointer, 2> m = {{&vt, &*ii}};
          visit_callback(*this, "", m);
          collections[0]->insert(collections[0]->end(), vt);
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

    class ParameterizeVisitor : public RewriteVisitorBase<ParameterizeVisitor> {
      SourceLocation m_location;
      List<TreePtr<Anonymous> > m_elements;
      unsigned m_depth;
      
    public:
      ParameterizeVisitor(const SourceLocation& location, const List<TreePtr<Anonymous> >& elements, unsigned depth)
      : m_location(location), m_elements(elements), m_depth(depth) {}

      template<typename T>
      TreePtr<T> visit_tree_ptr(TreePtr<T>& ptr) {
        
        
        return ptr->parameterize(m_location, m_elements, m_depth);
      }
    };

    class SpecializeVisitor : public RewriteVisitorBase<SpecializeVisitor> {
      SourceLocation m_location;
      List<TreePtr<Term> > m_values;
      unsigned m_depth;

    public:
      SpecializeVisitor(const SourceLocation& location, const List<TreePtr<Term> >& values, unsigned depth)
      : m_location(location), m_values(values), m_depth(depth) {}

      template<typename T>
      TreePtr<T> visit_tree_ptr(TreePtr<T>& ptr) {
        return ptr->specialize(m_location, m_values, m_depth);
      }
    };

    template<typename Derived>
    struct TermWrapper : NonConstructible {
      static const TreeBase* parameterize(const Term *self, const SourceLocation *location, const void *elements_vtable, void *elements_object, unsigned depth) {
        Derived rewritten;
        boost::array<const Derived*, 1> ptrs = {{&rewritten, const_cast<Derived*>(static_cast<const Derived*>(self))}};
        ParameterizeVisitor pv(List<TreePtr<Term> >(elements_vtable, elements_object), depth);
        visit_members(pv, ptrs);
        return pv.changed() ? new Derived(rewritten) : self;
      }
      
      static const TreeBase* specialize(const Term *self, const SourceLocation *location, const void *values_vtable, void *values_object, unsigned depth) {
        Derived rewritten;
        boost::array<const Derived*, 1> ptrs = {{&rewritten, const_cast<Derived*>(static_cast<const Derived*>(self))}};
        SpecializeVisitor pv(List<TreePtr<Term> >(values_vtable, values_object), depth);
        visit_members(pv, ptrs);
        return pv.changed() ? new Derived(rewritten) : self;
      }
      
      static const TreeBase* interface_search(const Term *self, const TreeBase *interface, const void *parameters_vtable, void *parameters_object) {
        TreePtr<> result = Derived::interface_search_impl(*static_cast<const Derived*>(self), tree_from_base<Interface>(interface), List<TreePtr<Term> >(parameters_vtable, parameters_object));
        return result.release();
      }
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TermWrapper<derived>::parameterize, \
    &::Psi::Compiler::TermWrapper<derived>::specialize, \
    &::Psi::Compiler::TermWrapper<derived>::interface_search \
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
      Type(CompileContext&, const SourceLocation&);
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_TERM(derived,name,super)

    /**
     * \brief Type of types.
     */
    class Metatype : public Term {
      friend class CompileContext;
      Metatype(CompileContext&, const SourceLocation&);
    public:
      static const TermVtable vtable;
    };

    /// \brief Is this a type?
    inline bool Term::is_type() const {return !type || dyn_tree_cast<Metatype>(type.get());}

    class Global;
    class Interface;

    /**
     * \brief Class used for error reporting.
     */
    class CompileError {
      CompileContext *m_compile_context;
      SourceLocation m_location;
      unsigned m_flags;
      const char *m_type;

    public:
      enum ErrorFlags {
        error_warning=1,
        error_internal=2
      };

      template<typename T>
      static std::string to_str(const T& t) {
        std::ostringstream ss;
        ss << t;
        return ss.str();
      }

      CompileError(CompileContext& compile_context, const SourceLocation& location, unsigned flags=0);

      void info(const std::string& message);
      void info(const SourceLocation& location, const std::string& message);
      void end();

      const SourceLocation& location() {return m_location;}

      template<typename T> void info(const T& message) {info(to_str(message));}
      template<typename T> void info(const SourceLocation& location, const T& message) {info(location, to_str(message));}
    };


    class Macro;
    class EvaluateContext;
    class ImplementationTerm;
    
    /**
     * \brief Low-level macro interface.
     *
     * \see Macro
     * \see MacroWrapper
     */
    struct MacroVtable {
      TreeVtable base;
      const TreeBase* (*evaluate) (const Macro*, const TreeBase*, const void*, void*, const TreeBase*, const SourceLocation*);
      const TreeBase* (*dot) (const Macro*, const TreeBase*, const SharedPtr<Parser::Expression>*,  const TreeBase*, const SourceLocation*);
    };

    class Macro : public Tree {
    public:
      typedef MacroVtable VtableType;
      static const SIVtable vtable;

      Macro(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value,
                              const List<SharedPtr<Parser::Expression> >& parameters,
                              const TreePtr<EvaluateContext>& evaluate_context,
                              const SourceLocation& location) const {
        return tree_from_base<Term>(derived_vptr(this)->evaluate(this, value.raw_get(), parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location), false);
      }

      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const SharedPtr<Parser::Expression>& parameter,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) const {
        return tree_from_base<Term>(derived_vptr(this)->dot(this, value.raw_get(), &parameter, evaluate_context.raw_get(), &location), false);
      }
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static const TreeBase* evaluate(const Macro *self,
                                const TreeBase*value,
                                const void *parameters_vtable,
                                void *parameters_object,
                                const TreeBase *evaluate_context,
                                const SourceLocation *location) {
        TreePtr<Term> result = Impl::evaluate_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }

      static const TreeBase* dot(const Macro *self,
                                 const TreeBase *value,
                                 const SharedPtr<Parser::Expression> *parameter,
                                 const TreeBase *evaluate_context,
                                 const SourceLocation *location) {
        TreePtr<Term> result = Impl::dot_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), *parameter, tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }
    };

#define PSI_COMPILER_MACRO(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroWrapper<derived>::evaluate, \
    &MacroWrapper<derived>::dot \
  }

    /**
     * \see EvaluateContext
     */
    struct EvaluateContextVtable {
      TreeVtable base;
      void (*lookup) (LookupResult<TreePtr<Term> >*, const EvaluateContext*, const String*);
    };

    class EvaluateContext : public Tree {
    public:
      typedef EvaluateContextVtable VtableType;
      static const SIVtable vtable;

      EvaluateContext(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      LookupResult<TreePtr<Term> > lookup(const String& name) const {
        ResultStorage<LookupResult<TreePtr<Term> > > result;
        derived_vptr(this)->lookup(result.ptr(), this, &name);
        return result.done();
      }
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct EvaluateContextWrapper : NonConstructible {
      static void lookup(LookupResult<TreePtr<Term> > *result, const EvaluateContext *self, const String *name) {
        new (result) LookupResult<TreePtr<Term> >(Impl::lookup_impl(*static_cast<const Derived*>(self), *name));
      }
    };

#define PSI_COMPILER_EVALUATE_CONTEXT(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &EvaluateContextWrapper<derived>::lookup \
  }

    class MacroEvaluateCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroEvaluateCallbackVtable {
      TreeVtable base;
      const TreeBase* (*evaluate) (const MacroEvaluateCallback*, const TreeBase*, const void*, void*, const TreeBase*, const SourceLocation*);
    };

    class MacroEvaluateCallback : public Tree {
    public:
      typedef MacroEvaluateCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroEvaluateCallback(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value, const List<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) const {
        return tree_from_base<Term>(derived_vptr(this)->evaluate(this, value.raw_get(), parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location), false);
      }
    };

    template<typename Derived>
    struct MacroEvaluateCallbackWrapper : NonConstructible {
      static const TreeBase* evaluate(const MacroEvaluateCallback *self, const TreeBase *value, const void *parameters_vtable, void *parameters_object, const TreeBase *evaluate_context, const SourceLocation *location) {
        TreePtr<Term> result = Derived::evaluate_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }
    };

#define PSI_COMPILER_MACRO_EVALUATE_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroEvaluateCallbackWrapper<derived>::evaluate \
  }

    class MacroDotCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroDotCallbackVtable {
      TreeVtable base;
      Term* (*dot) (const MacroDotCallback*, const TreeBase*, const TreeBase*, const SourceLocation*);
    };

    /**
     * \brief Wrapper class to ease using MacroEvaluateCallbackVtable from C++.
     */
    class MacroDotCallback : public Tree {
    public:
      typedef MacroDotCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroDotCallback(CompileContext& compile_context, const Psi::Compiler::SourceLocation& location)
      : Tree(compile_context, location) {
      }

      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) const {
        return TreePtr<Term>(derived_vptr(this)->dot(this, value.raw_get(), evaluate_context.raw_get(), &location), false);
      }
    };

    /**
     * \brief Wrapper class to ease implementing MacroEvaluateCallbackVtable in C++.
     */
    template<typename Derived>
    struct MacroDotCallbackWrapper : NonConstructible {
      static Term* dot(MacroDotCallback *self, Term *value, EvaluateContext *evaluate_context, const SourceLocation *location) {
        return Derived::dot_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), TreePtr<EvaluateContext>(evaluate_context), *location).release();
      }
    };

#define PSI_COMPILER_MACRO_DOT_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super) \
    &MacroDotCallbackWrapper<derived>::dot \
  }

    class Interface : public Tree {
      unsigned m_n_parameters;
      /// \brief If the target of this interface is a compile-time type, this value gives the type of tree we're looking for.
      TreeVtable *m_compile_time_type;
      /// \brief If the target of this interface is a run-time value, this gives the type of that value.
      TreePtr<Term> m_run_time_type;

    public:
      static const TreeVtable vtable;
      Interface(CompileContext&, const SourceLocation&);

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Tree>(v);
        v("run_time_type", &Interface::m_run_time_type);
      }
    };

    /**
     * \brief Context for objects used during compilation.
     *
     * This manages state which is global to the compilation and
     * compilation object lifetimes.
     */
    class CompileContext {
      friend class TreeBase;
      friend class RunningTreeCallback;
      struct TreeBaseDisposer;

      std::ostream *m_error_stream;
      bool m_error_occurred;
      RunningTreeCallback *m_running_completion_stack;

      boost::intrusive::list<TreeBase, boost::intrusive::constant_time_size<false> > m_gc_list;

      SourceLocation m_root_location;

      TreePtr<Interface> m_macro_interface;
      TreePtr<Interface> m_argument_passing_interface;
      TreePtr<Type> m_empty_type;
      TreePtr<Term> m_metatype;

    public:
      CompileContext(std::ostream *error_stream);
      ~CompileContext();

      /// \brief Return the stream used for error reporting.
      std::ostream& error_stream() {return *m_error_stream;}

      /// \brief Returns true if an error has occurred during compilation.
      bool error_occurred() const {return m_error_occurred;}
      /// \brief Call this to indicate an unrecoverable error occurred at some point during compilation.
      void set_error_occurred() {m_error_occurred = true;}

      void error(const SourceLocation&, const std::string&, unsigned=0);
      void error_throw(const SourceLocation&, const std::string&, unsigned=0) PSI_ATTRIBUTE((PSI_NORETURN));

      template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, CompileError::to_str(message), flags);}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, CompileError::to_str(message), flags);}

      void completion_state_push(RunningTreeCallback *state);
      void completion_state_pop();

      void* jit_compile(const TreePtr<Global>&);

      TreePtr<Global> tree_from_address(const SourceLocation&, const TreePtr<Type>&, void*);

      const SourceLocation& root_location() {return m_root_location;}

      /// \brief Get the Macro interface.
      const TreePtr<Interface>& macro_interface() {return m_macro_interface;}
      /// \brief Get the argument passing descriptor interface.
      const TreePtr<Interface>& argument_passing_info_interface() {return m_argument_passing_interface;}
      /// \brief Get the empty type.
      const TreePtr<Type>& empty_type() {return m_empty_type;}
      /// \brief Get the type of types.
      const TreePtr<Term>& metatype() {return m_metatype;}
    };

    class Block;

    TreePtr<Term> compile_expression(const SharedPtr<Parser::Expression>&, const TreePtr<EvaluateContext>&, const LogicalSourceLocationPtr&);
    TreePtr<Block> compile_statement_list(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);

    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<Term> >&, const TreePtr<EvaluateContext>&);
    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<Term> >&);

    TreePtr<> interface_lookup(const TreePtr<Interface>&, const List<TreePtr<Term> >&, const SourceLocation&);
    void interface_cast_check(const TreePtr<Interface>&, const List<TreePtr<Term> >&, const TreePtr<>&, const SourceLocation&, const TreeVtable*);

    template<typename T>
    TreePtr<T> interface_lookup_as(const TreePtr<Interface>& interface, const TreePtr<Term>& parameter, const SourceLocation& location) {
      boost::array<TreePtr<Term>, 1> parameters;
      parameters[0] = parameter;
      TreePtr<> result = interface_lookup(interface, list_from_stl(parameters), location);
      interface_cast_check(interface, list_from_stl(parameters), result, location, reinterpret_cast<const TreeVtable*>(&T::vtable));
      return treeptr_cast<T>(result);
    }

    template<typename T>
    TreePtr<T> interface_lookup_as(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const SourceLocation& location) {
      TreePtr<> result = interface_lookup(interface, parameters, location);
      interface_cast_check(interface, parameters, result, location, reinterpret_cast<const TreeVtable*>(&T::vtable));
      return treeptr_cast<T>(result);
    }

    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroEvaluateCallback>&, const std::map<String, TreePtr<MacroDotCallback> >&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroEvaluateCallback>&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<MacroDotCallback> >&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&);
    TreePtr<Term> make_macro_term(CompileContext& compile_context, const SourceLocation& location, const TreePtr<Macro>& macro);
  }
}

#endif
