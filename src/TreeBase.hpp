#ifndef HPP_PSI_COMPILER_TREEBASE
#define HPP_PSI_COMPILER_TREEBASE

#include <set>

#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits/is_convertible.hpp>

#include "ObjectBase.hpp"
#include "SourceLocation.hpp"

namespace Psi {
  namespace Compiler {
    class TreeBase;
    class TreeCallback;
    class Tree;
    class Term;
    class CompileContext;

    class TreePtrBase {
      typedef void (TreePtrBase::*safe_bool_type) () const;
      void safe_bool_true() const {}

      mutable ObjectPtr<const TreeBase> m_ptr;

      const Tree* get_helper() const;
      void update_chain(const TreeBase *ptr) const;

    protected:
      void swap(TreePtrBase& other) {m_ptr.swap(other.m_ptr);}
      
    public:
      TreePtrBase() {}
      explicit TreePtrBase(const TreeBase *ptr, bool add_ref) : m_ptr(ptr, add_ref) {}
      
      const Tree* get() const;
      const TreeBase* raw_get() const {return m_ptr.get();}
      const ObjectPtr<const TreeBase>& raw_ptr_get() const {return m_ptr;}
      const TreeBase* release() {return m_ptr.release();}

      operator safe_bool_type () const {return get() ? &TreePtrBase::safe_bool_true : 0;}
      bool operator ! () const {return !get();}
      bool operator == (const TreePtrBase& other) const {return get() == other.get();};
      bool operator != (const TreePtrBase& other) const {return get() != other.get();};
      bool operator < (const TreePtrBase& other) const {return get() < other.get();};

      /// \brief Get the compile context for this Tree, without evaluating the Tree.
      CompileContext& compile_context() const {return m_ptr.compile_context();}
      const SourceLocation& location() const;
      
#ifdef PSI_DEBUG
      void debug_print() const;
#endif
    };
    
    inline std::size_t hash_value(const TreePtrBase& ptr) {return boost::hash_value(ptr.get());}

    template<typename T=Tree>
    class TreePtr : public TreePtrBase {
      template<typename U> friend TreePtr<U> tree_from_base(const TreeBase*);
      template<typename U> friend TreePtr<U> tree_from_base_take(const TreeBase*);
      TreePtr(const TreeBase *src, bool add_ref) : TreePtrBase(src, add_ref) {}

    public:
      TreePtr() {}
      explicit TreePtr(const T *ptr) : TreePtrBase(ptr, true) {}
      template<typename U> TreePtr(const TreePtr<U>& src) : TreePtrBase(src) {BOOST_STATIC_ASSERT((boost::is_convertible<U*, T*>::value));}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {TreePtr<T>(src).swap(*this); return *this;}

      const T* get() const;
      const T* operator -> () const {return get();}

      void reset(const T *ptr=NULL) {TreePtr<T>(ptr).swap(*this);}
      void swap(TreePtr<T>& other) {TreePtrBase::swap(other);}
    };

    /**
     * Get a TreePtr from a point to a TreeBase.
     *
     * This should only be used in wrapper functions, since otherwise the type of \c base
     * should be statically known.
     */
    template<typename T>
    TreePtr<T> tree_from_base(const TreeBase *base) {
      return TreePtr<T>(base, true);
    }
    
    /**
     * Get a TreePtr from a pointer to a TreeBase.
     * 
     * This is used where pointers are returned to wrapper functions so that the reference
     * count need not be incremented. This is a separate function to tree_from_base because
     * this sort of manual reference count management can easily lead to bugs, and and additional
     * function makes this easier to look for.
     */
    template<typename T>
    TreePtr<T> tree_from_base_take(const TreeBase *base) {
      return TreePtr<T>(base, false);
    };

    class VisitorPlaceholder {
    public:
      template<typename T>
      VisitorPlaceholder& operator () (const char *name PSI_ATTRIBUTE((PSI_UNUSED)), T& member PSI_ATTRIBUTE((PSI_UNUSED))) {
        return *this;
      }
    };

    /// \see TreeBase
    struct TreeBaseVtable {
      ObjectVtable base;
      bool is_callback;
    };

    /**
     * Extends Object for lazy evaluation of Trees.
     *
     * Two types derive from this: Tree, which holds values, and TreeCallbackHolder, which
     * encapsulates a callback to return a Tree.
     */
    class TreeBase : public Object {
      friend class CompileContext;
      friend class RunningTreeCallback;
      friend class TreeCallback;

      SourceLocation m_location;

    public:
      typedef TreeBaseVtable VtableType;
      static const SIVtable vtable;
      
      TreeBase(const TreeBaseVtable *vptr, CompileContext& compile_context, const SourceLocation& location);

      const SourceLocation& location() const {return m_location;}

      template<typename Visitor>
      static void visit(Visitor& visitor PSI_ATTRIBUTE((PSI_UNUSED))) {}
    };

    /// \brief Get the location of this Tree, without evaluating the Tree.
    inline const SourceLocation& TreePtrBase::location() const {return m_ptr->location();}

#define PSI_COMPILER_TREE_BASE(is_callback,derived,name,super) { \
    PSI_COMPILER_OBJECT(derived,name,super), \
    (is_callback) \
  }

    /**
     * Data structure for performing recursive object visiting. This stores objects
     * to visit in a queue and remembers previously visited objects so that nothing
     * is visited twice.
     */
    template<typename T>
    class VisitQueue {
      PSI_STD::vector<T> m_queue;
      PSI_STD::set<T> m_visited;
      
    public:
      bool empty() const {return m_queue.empty();}
      T pop() {T x = m_queue.back(); m_queue.pop_back(); return x;}
      
      void push(const T& x) {
        if (m_visited.find(x) == m_visited.end()) {
          m_visited.insert(x);
          m_queue.push_back(x);
        }
      }
    };
  
    /// \see Tree
    struct TreeVtable {
      TreeBaseVtable base;
      void (*complete) (Tree*,VisitQueue<TreePtr<> >*);
    };

    class Tree : public TreeBase {
    public:
      static const SIVtable vtable;
      
      typedef TreeVtable VtableType;

      Tree(const TreeVtable *vptr, CompileContext& compile_context, const SourceLocation& location);

      void complete();
      
      template<typename V> static void visit(V& v) {visit_base<TreeBase>(v);}
    };

    template<typename T> const T* TreePtr<T>::get() const {
      const Tree *t = TreePtrBase::get();
      PSI_ASSERT(!t || si_is_a(t, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<const T*>(t);
    };
    
    template<typename T>
    bool tree_isa(const Tree *ptr) {
      return si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable));
    }
    
    template<typename T, typename U>
    bool tree_isa(const TreePtr<U>& ptr) {
      return tree_isa<T>(ptr.get());
    }

    template<typename T>
    const T* tree_cast(const Tree *ptr) {
      PSI_ASSERT(tree_isa<T>(ptr));
      return static_cast<const T*>(ptr);
    }

    template<typename T>
    const T* dyn_tree_cast(const Tree *ptr) {
      return tree_isa<T>(ptr) ? static_cast<const T*>(ptr) : 0;
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
    class CompleteVisitor : public ObjectVisitorBase<CompleteVisitor> {
      VisitQueue<TreePtr<> > *m_queue;
    public:
      CompleteVisitor(VisitQueue<TreePtr<> > *queue) : m_queue(queue) {
      }
      
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        if (ptr)
          m_queue->push(ptr);
      }
    };

    template<typename Derived>
    struct TreeWrapper : NonConstructible {
      static void complete(Tree *self, VisitQueue<TreePtr<> > *queue) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        CompleteVisitor p(queue);
        visit_members(p, a);
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_TREE_BASE(false,derived,name,super), \
    &::Psi::Compiler::TreeWrapper<derived>::complete \
  }

#define PSI_COMPILER_VPTR_UP(super,vptr) (PSI_ASSERT(si_derived(reinterpret_cast<const SIVtable*>(&super::vtable), reinterpret_cast<const SIVtable*>(vptr))), reinterpret_cast<const super::VtableType*>(vptr))
#define PSI_COMPILER_TREE_ABSTRACT(name,super) PSI_COMPILER_SI_ABSTRACT(name,&super::vtable)

    inline const Tree* TreePtrBase::get() const {
      return (!m_ptr || !derived_vptr(m_ptr.get())->is_callback) ? static_cast<const Tree*>(m_ptr.get()) : get_helper();
    }

    /// \see TreeCallback
    struct TreeCallbackVtable {
      TreeBaseVtable base;
      const TreeBase* (*evaluate) (const TreeCallback*);
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

    public:
      typedef TreeCallbackVtable VtableType;
      static const SIVtable vtable;

      TreeCallback(const TreeCallbackVtable *vptr, CompileContext&, const SourceLocation&);

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
      static const TreeBase* evaluate(const TreeCallback *self) {
        return Derived::evaluate_impl(*static_cast<const Derived*>(self)).release();
      }
    };

#define PSI_COMPILER_TREE_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE_BASE(true,derived,name,super), \
    &::Psi::Compiler::TreeCallbackWrapper<derived>::evaluate \
  }

    /// To get past preprocessor's inability to understand template parameters
    template<typename T, typename Functor>
    struct TreeCallbackImplArgs {
      typedef T TreeResultType;
      typedef Functor FunctionType;
    };

    template<typename Args>
    class TreeCallbackImpl : public TreeCallback {
    public:
      typedef typename Args::TreeResultType TreeResultType;
      typedef typename Args::FunctionType FunctionType;

    private:
      mutable FunctionType *m_function;

    public:
      static const TreeCallbackVtable vtable;

      TreeCallbackImpl(CompileContext& compile_context, const SourceLocation& location, const FunctionType& function)
      : TreeCallback(&vtable, compile_context, location), m_function(new FunctionType(function)) {
      }

      ~TreeCallbackImpl() {
        if (m_function)
          delete m_function;
      }

      static TreePtr<TreeResultType> evaluate_impl(const TreeCallbackImpl& self) {
        PSI_ASSERT(self.m_function);
        boost::scoped_ptr<FunctionType> function_copy(self.m_function);
        self.m_function = NULL;
        return function_copy->evaluate(tree_from_base<TreeResultType>(&self));
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<TreeCallback>(v);
        v("function", &TreeCallbackImpl::m_function);
      }
    };

#ifndef PSI_DEBUG
    template<typename T>
    const TreeCallbackVtable TreeCallbackImpl<T>::vtable = PSI_COMPILER_TREE_CALLBACK(TreeCallbackImpl<T>, "(anonymous)", TreeCallback);
#else
    template<typename T>
    const TreeCallbackVtable TreeCallbackImpl<T>::vtable = PSI_COMPILER_TREE_CALLBACK(TreeCallbackImpl<T>, typeid(typename T::FunctionType).name(), TreeCallback);
#endif

    /**
     * \brief Make a lazily evaluated Tree from a C++ functor object.
     */
    template<typename T, typename Callback>
    TreePtr<T> tree_callback(CompileContext& compile_context, const SourceLocation& location, const Callback& callback) {
      return tree_from_base<T>(new TreeCallbackImpl<TreeCallbackImplArgs<T, Callback> >(compile_context, location, callback));
    }

    /**
     * \brief Make a lazily evaluated Tree from a C++ functor object.
     */
    template<typename Callback>
    TreePtr<typename Callback::TreeResultType> tree_callback(CompileContext& compile_context, const SourceLocation& location, const Callback& callback) {
      return tree_from_base<typename Callback::TreeResultType>(new TreeCallbackImpl<TreeCallbackImplArgs<typename Callback::TreeResultType, Callback> >(compile_context, location, callback));
    }
    
    template<typename T, typename F>
    class TreePropertyWrapper {
      TreePtr<T> m_tree;
      F m_func;
      
    public:
      typedef typename F::TreeResultType TreeResultType;
      
      TreePropertyWrapper(const TreePtr<T>& tree, const F& func)
      : m_tree(tree), m_func(func) {
      }

      TreePtr<TreeResultType> evaluate(const TreePtr<TreeResultType>&) {
        return m_func(m_tree);
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("tree", &TreePropertyWrapper::m_tree);
      }
    };

    /**
     * Wrapper for simple functors on trees. Note that the functor \c f should not contain any references
     * to other trees since there is no way for the GC to see them.
     */
    template<typename T, typename Callback>
    TreePtr<typename Callback::TreeResultType> tree_property(const TreePtr<T>& tree, const Callback& callback, const SourceLocation& location) {
      return tree_callback(tree.compile_context(), location, TreePropertyWrapper<T, Callback>(tree, callback));
    }
    
    template<typename A, typename B>
    class TreeAttributeFunction {
      TreePtr<B> A::*m_ptr;
      
    public:
      typedef B TreeResultType;
      
      TreeAttributeFunction(TreePtr<B> A::*ptr) : m_ptr(ptr) {
      }
      
      TreePtr<B> operator () (const TreePtr<A>& ptr) {
        return ptr.get()->*m_ptr;
      }
    };
    
    /**
     * \brief Delayed member attribute getter.
     * 
     * 
     * This should be used to access attributes of trees when it is possible 
     */
    template<typename A, typename B>
    TreePtr<B> tree_attribute(const TreePtr<A>& tree, TreePtr<B> A::*ptr) {
      return tree_property(tree, TreeAttributeFunction<A,B>(ptr), tree.location());
    }
  }
}

#endif