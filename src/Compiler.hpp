#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef PSI_DEBUG
#include <typeinfo>
#endif

#include <boost/array.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits/is_convertible.hpp>

#include "CppCompiler.hpp"
#include "GarbageCollection.hpp"
#include "Runtime.hpp"
#include "SourceLocation.hpp"
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
    bool si_derived(const SIVtable *base, const SIVtable *derived);

    template<typename T>
    const typename T::VtableType* derived_vptr(T *ptr) {
      return reinterpret_cast<const typename T::VtableType*>(si_vptr(ptr));
    }

    class Object;
    class TreeBase;
    class Tree;
    class Type;
    class CompileContext;

    template<typename T>
    class ObjectPtr {
      typedef void (ObjectPtr::*safe_bool_type) () const;
      void safe_bool_true() const {}

      T *m_ptr;

      void initialize(T *ptr, bool add_ref) {
        m_ptr = ptr;
        if (add_ref && m_ptr)
          ++m_ptr->m_reference_count;
      }

    public:
      ~ObjectPtr();
      ObjectPtr() : m_ptr(NULL) {}
      explicit ObjectPtr(T *ptr, bool add_ref) {initialize(ptr, add_ref);}
      ObjectPtr(const ObjectPtr& src) {initialize(src.m_ptr, true);}
      template<typename U> ObjectPtr(const ObjectPtr<U>& src) {initialize(src.get(), true);}
      ObjectPtr& operator = (const ObjectPtr& src) {ObjectPtr(src).swap(*this); return *this;}
      template<typename U> ObjectPtr& operator = (const ObjectPtr<U>& src) {ObjectPtr<T>(src).swap(*this); return *this;}

      T* get() const {return m_ptr;}
      T* release() {T *tmp = m_ptr; m_ptr = NULL; return tmp;}
      void swap(ObjectPtr& other) {std::swap(m_ptr, other.m_ptr);}
      void reset(T *ptr=NULL, bool add_ref=true) {ObjectPtr<T>(ptr, add_ref).swap(*this);}

      T& operator * () const {return *get();}
      T* operator -> () const {return get();}

      operator safe_bool_type () const {return get() ? &ObjectPtr::safe_bool_true : 0;}
      bool operator ! () const {return !get();}
      template<typename U> bool operator == (const ObjectPtr<U>& other) const {return get() == other.get();};
      template<typename U> bool operator != (const ObjectPtr<U>& other) const {return get() != other.get();};
      template<typename U> bool operator < (const ObjectPtr<U>& other) const {return get() < other.get();};

      /// \brief Get the compile context for this Object.
      CompileContext& compile_context() const {
        PSI_ASSERT(m_ptr);
        return m_ptr->compile_context();
      }
    };

    class TreePtrBase {
      typedef void (TreePtrBase::*safe_bool_type) () const;
      void safe_bool_true() const {}

      mutable ObjectPtr<TreeBase> m_ptr;

      Tree* get_helper() const;
      void update_chain(TreeBase *ptr) const;

    protected:
      void swap(TreePtrBase& other) {m_ptr.swap(other.m_ptr);}
      
    public:
      TreePtrBase() {}
      explicit TreePtrBase(TreeBase *ptr, bool add_ref) : m_ptr(ptr, add_ref) {}
      
      Tree* get() const;
      TreeBase* raw_get() const {return m_ptr.get();}
      const ObjectPtr<TreeBase>& raw_ptr_get() const {return m_ptr;}
      TreeBase* release() {return m_ptr.release();}

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
      template<typename U> friend TreePtr<U> tree_from_base(TreeBase*);
      template<typename U> friend TreePtr<U> tree_from_base_take(TreeBase*);
      TreePtr(TreeBase *src, bool add_ref) : TreePtrBase(src, add_ref) {}

    public:
      TreePtr() {}
      explicit TreePtr(T *ptr) : TreePtrBase(ptr, true) {}
      template<typename U> TreePtr(const TreePtr<U>& src) : TreePtrBase(src) {BOOST_STATIC_ASSERT((boost::is_convertible<U*, T*>::value));}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {TreePtr<T>(src).swap(*this); return *this;}

      T* get() const;
      T* operator -> () const {return get();}

      void reset(T *ptr=NULL) {TreePtr<T>(ptr).swap(*this);}
      void swap(TreePtr<T>& other) {TreePtrBase::swap(other);}
    };

    /**
     * Get a TreePtr from a point to a TreeBase.
     *
     * This should only be used in wrapper functions, since otherwise the type of \c base
     * should be statically known.
     */
    template<typename T>
    TreePtr<T> tree_from_base(TreeBase *base) {
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
    TreePtr<T> tree_from_base_take(TreeBase *base) {
      return TreePtr<T>(base, false);
    };

    class VisitorPlaceholder {
    public:
      template<typename T>
      VisitorPlaceholder& operator () (const char *name PSI_ATTRIBUTE((PSI_UNUSED)), T& member PSI_ATTRIBUTE((PSI_UNUSED))) {
        return *this;
      }
    };

    class Object;
    class Tree;
    class TreeBase;
    class TreeCallback;
    class Term;

    /// \see Object
    struct ObjectVtable {
      SIVtable base;
      void (*destroy) (Object*);
      void (*gc_increment) (Object*);
      void (*gc_decrement) (Object*);
      void (*gc_clear) (Object*);
    };

    /**
     * Extends SIBase to participate in garbage collection.
     */
    class Object : public SIBase, public boost::intrusive::list_base_hook<> {
      friend class CompileContext;
      friend class GCVisitorIncrement;
      friend class GCVisitorDecrement;
      template<typename> friend class ObjectPtr;

      mutable std::size_t m_reference_count;
      CompileContext *m_compile_context;

    public:
      typedef ObjectVtable VtableType;
      static const SIVtable vtable;

      Object(const ObjectVtable *vtable, CompileContext& compile_context);
      ~Object();

      CompileContext& compile_context() const {return *m_compile_context;}
      
      template<typename V> static void visit(V&) {}
    };
    
    template<typename T>
    ObjectPtr<T>::~ObjectPtr() {
      if (m_ptr) {
        if (!--m_ptr->m_reference_count) {
          const Object *cptr = m_ptr;
          Object *optr = const_cast<Object*>(cptr);
          derived_vptr(optr)->destroy(optr);
        }
      }
    }

    /**
     * \brief Base classes for gargabe collection phase
     * implementations.
     */
    template<typename Derived>
    class ObjectVisitorBase {
      Derived& derived() {
        return *static_cast<Derived*>(this);
      }

    public:
      template<typename T>
      void visit_base(const boost::array<T*,1>& c) {
        if (derived().do_visit_base(VisitorTag<T>()))
          visit_members(derived(), c);
      }
      
      template<typename T>
      bool do_visit_base(VisitorTag<T>) {
        return true;
      }

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
        if (*obj[0]) {
          boost::array<T*, 1> star = {{*obj[0]}};
          visit_callback(*this, NULL, star);
        }
      }

      /// Shared pointers cannot reference trees (this would break the GC), so they are ignored.
      template<typename T>
      void visit_object(const char*, const boost::array<SharedPtr<T>*,1>&) {
      }

      template<typename T>
      void visit_object(const char*, const boost::array<ObjectPtr<T>*,1>& ptr) {
        derived().visit_object_ptr(*ptr[0]);
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
    class GCVisitorIncrement : public ObjectVisitorBase<GCVisitorIncrement> {
    public:
      template<typename T>
      void visit_object_ptr(const ObjectPtr<T>& ptr) {
        if (ptr)
          ++ptr->m_reference_count;
      }
      
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        visit_object_ptr(ptr.raw_ptr_get());
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorDecrement : public ObjectVisitorBase<GCVisitorDecrement> {
    public:
      template<typename T>
      void visit_object_ptr(const ObjectPtr<T>& ptr) {
        if (ptr)
          --ptr->m_reference_count;
      }
      
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        visit_object_ptr(ptr.raw_ptr_get());
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorClear : public ObjectVisitorBase<GCVisitorClear> {
    public:
      template<typename T>
      void visit_sequence(const char*, const boost::array<T*,1>& seq) {
        seq[0]->clear();
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*,1>& maps) {
        maps[0]->clear();
      }

      template<typename T>
      void visit_object_ptr(ObjectPtr<T>& ptr) {
        ptr.reset();
      }

      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        ptr.reset();
      }
    };

    template<typename Derived>
    struct ObjectWrapper : NonConstructible {
      static void destroy(Object *self) {
        delete static_cast<Derived*>(self);
      }

      static void gc_increment(Object *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorIncrement p;
        visit_members(p, a);
      }

      static void gc_decrement(Object *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorDecrement p;
        visit_members(p, a);
      }

      static void gc_clear(Object *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorClear p;
        visit_members(p, a);
      }
    };

#define PSI_COMPILER_OBJECT(derived,name,super) { \
    PSI_COMPILER_SI(name,&super::vtable), \
    &::Psi::Compiler::ObjectWrapper<derived>::destroy, \
    &::Psi::Compiler::ObjectWrapper<derived>::gc_increment, \
    &::Psi::Compiler::ObjectWrapper<derived>::gc_decrement, \
    &::Psi::Compiler::ObjectWrapper<derived>::gc_clear \
  }

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
      PsiBool (*match) (const Tree*,const Tree*,const void*,void*,unsigned);
      Tree* (*parameterize_evaluations) (const Tree*,const void*,void*,unsigned);
    };

    /**
     * Used to store pointers to tree types in objects, in order to work with the
     * visitor system.
     */
    class SIType {
      const SIVtable *m_vptr;
      
    public:
      SIType() : m_vptr(NULL) {}
      SIType(const SIVtable *vptr) : m_vptr(vptr) {}
    
      const SIVtable* get() const {return m_vptr;}
      operator const SIVtable* () const {return get();}
      const SIVtable* operator -> () const {return get();}
      
      template<typename Visitor>
      static void visit(Visitor&) {}
    };

    class Tree : public TreeBase {
    public:
      static const SIVtable vtable;
      static const bool match_unique = true;
      
      typedef TreeVtable VtableType;

      Tree(const TreeVtable *vptr, CompileContext& compile_context, const SourceLocation& location);

      void complete();
      bool match(const TreePtr<Tree>& value, const List<TreePtr<Term> >& wildcards, unsigned depth=0);
      bool match(const TreePtr<Tree>& value);
      
      template<typename V> static void visit(V& v) {visit_base<TreeBase>(v);}
    };

    template<typename T> T* TreePtr<T>::get() const {
      Tree *t = TreePtrBase::get();
      PSI_ASSERT(!t || si_is_a(t, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<T*>(t);
    };

    template<typename T>
    T* tree_cast(Tree *ptr) {
      PSI_ASSERT(si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<T*>(ptr);
    }

    template<typename T>
    T* dyn_tree_cast(Tree *ptr) {
      return si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable)) ? static_cast<T*>(ptr) : 0;
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
      static void complete(Tree *self, VisitQueue<TreePtr<> > *queue) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        CompleteVisitor p(queue);
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
      
      static Tree* parameterize_evaluations(const Tree *self, const void *parameters_vtable PSI_ATTRIBUTE((PSI_UNUSED)), void *parameters_obj PSI_ATTRIBUTE((PSI_UNUSED)), unsigned depth PSI_ATTRIBUTE((PSI_UNUSED))) {
        PSI_FAIL("not implemented");
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_TREE_BASE(false,derived,name,super), \
    &::Psi::Compiler::TreeWrapper<derived>::complete, \
    &::Psi::Compiler::TreeWrapper<derived>::match, \
    &::Psi::Compiler::TreeWrapper<derived>::parameterize_evaluations \
  }

#define PSI_COMPILER_VPTR_UP(super,vptr) (PSI_ASSERT(si_derived(reinterpret_cast<const SIVtable*>(&super::vtable), reinterpret_cast<const SIVtable*>(vptr))), reinterpret_cast<const super::VtableType*>(vptr))
#define PSI_COMPILER_TREE_ABSTRACT(name,super) PSI_COMPILER_SI_ABSTRACT(name,&super::vtable)

    inline Tree* TreePtrBase::get() const {
      return (!m_ptr || !derived_vptr(m_ptr.get())->is_callback) ? static_cast<Tree*>(m_ptr.get()) : get_helper();
    }

    /// \see TreeCallback
    struct TreeCallbackVtable {
      TreeBaseVtable base;
      TreeBase* (*evaluate) (TreeCallback*);
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
      static TreeBase* evaluate(TreeCallback *self) {
        return Derived::evaluate_impl(*static_cast<Derived*>(self)).release();
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
      FunctionType *m_function;

    public:
      static const TreeCallbackVtable vtable;

      TreeCallbackImpl(CompileContext& compile_context, const SourceLocation& location, const FunctionType& function)
      : TreeCallback(&vtable, compile_context, location), m_function(new FunctionType(function)) {
      }

      ~TreeCallbackImpl() {
        if (m_function)
          delete m_function;
      }

      static TreePtr<TreeResultType> evaluate_impl(TreeCallbackImpl& self) {
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

    class Anonymous;
    class Term;
    class Interface;

    struct TermVtable {
      TreeVtable base;
      TreeBase* (*parameterize) (Term*, const SourceLocation*, const void*, void*, unsigned);
      TreeBase* (*specialize) (Term*, const SourceLocation*, const void*, void*, unsigned);
      TreeBase* (*interface_search) (Term*, TreeBase*, const void*, void*);
    };

    class Term : public Tree {
      friend class Metatype;

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;

      static const SIVtable vtable;

      Term(const TermVtable *vtable, CompileContext& context, const SourceLocation& location);
      Term(const TermVtable *vtable, const TreePtr<Term>&, const SourceLocation&);

      /// \brief The type of this term.
      TreePtr<Term> type;

      bool is_type() const;

      /// \brief Replace anonymous terms in the list by parameters
      TreePtr<Term> parameterize(const SourceLocation& location, const List<TreePtr<Anonymous> >& elements, unsigned depth=0) {
        return tree_from_base_take<Term>(derived_vptr(this)->parameterize(this, &location, elements.vptr(), elements.object(), depth));
      }

      /// \brief Replace parameter terms in this tree by given values
      TreePtr<Term> specialize(const SourceLocation& location, const List<TreePtr<Term> >& values, unsigned depth=0) {
        return tree_from_base_take<Term>(derived_vptr(this)->specialize(this, &location, values.vptr(), values.object(), depth));
      }

      TreePtr<> interface_search(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) {
        return tree_from_base_take<Tree>(derived_vptr(this)->interface_search(this, interface.raw_get(), parameters.vptr(), parameters.object()));
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
      static TreeBase* parameterize(Term *self, const SourceLocation *location, const void *elements_vtable, void *elements_object, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<Derived*>(self)}};
        ParameterizeVisitor pv(*location, List<TreePtr<Anonymous> >(elements_vtable, elements_object), depth);
        visit_members(pv, ptrs);
        return TreePtr<Derived>(pv.changed() ? new Derived(rewritten) : static_cast<Derived*>(self)).release();
      }
      
      static TreeBase* specialize(Term *self, const SourceLocation *location, const void *values_vtable, void *values_object, unsigned depth) {
        Derived rewritten(self->compile_context(), *location);
        boost::array<const Derived*, 2> ptrs = {{&rewritten, static_cast<Derived*>(self)}};
        SpecializeVisitor pv(*location, List<TreePtr<Term> >(values_vtable, values_object), depth);
        visit_members(pv, ptrs);
        return TreePtr<Derived>(pv.changed() ? new Derived(rewritten) : static_cast<Derived*>(self)).release();
      }
      
      static TreeBase* interface_search(Term *self, TreeBase *interface, const void *parameters_vtable, void *parameters_object) {
        TreePtr<> result = Derived::interface_search_impl(*static_cast<Derived*>(self), tree_from_base<Interface>(interface), List<TreePtr<Term> >(parameters_vtable, parameters_object));
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
      TreeBase* (*evaluate) (Macro*, TreeBase*, const void*, void*, TreeBase*, const SourceLocation*);
      TreeBase* (*dot) (Macro*, TreeBase*, const SharedPtr<Parser::Expression>*,  TreeBase*, const SourceLocation*);
    };

    class Macro : public Tree {
    public:
      typedef MacroVtable VtableType;
      static const SIVtable vtable;

      Macro(const MacroVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value,
                              const List<SharedPtr<Parser::Expression> >& parameters,
                              const TreePtr<EvaluateContext>& evaluate_context,
                              const SourceLocation& location) {
        return tree_from_base_take<Term>(derived_vptr(this)->evaluate(this, value.raw_get(), parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location));
      }

      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const SharedPtr<Parser::Expression>& parameter,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) {
        return tree_from_base_take<Term>(derived_vptr(this)->dot(this, value.raw_get(), &parameter, evaluate_context.raw_get(), &location));
      }
      
      template<typename V> static void visit(V& v) {visit_base<Tree>(v);}
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static TreeBase* evaluate(Macro *self,
                                TreeBase*value,
                                const void *parameters_vtable,
                                void *parameters_object,
                                TreeBase *evaluate_context,
                                const SourceLocation *location) {
        TreePtr<Term> result = Impl::evaluate_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }

      static TreeBase* dot(Macro *self,
                           TreeBase *value,
                           const SharedPtr<Parser::Expression> *parameter,
                           TreeBase *evaluate_context,
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
      void (*lookup) (LookupResult<TreePtr<Term> >*, EvaluateContext*, const String*, const SourceLocation*, TreeBase*);
    };

    class EvaluateContext : public Tree {
    public:
      typedef EvaluateContextVtable VtableType;
      static const SIVtable vtable;

      EvaluateContext(const EvaluateContextVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      LookupResult<TreePtr<Term> > lookup(const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        ResultStorage<LookupResult<TreePtr<Term> > > result;
        derived_vptr(this)->lookup(result.ptr(), this, &name, &location, evaluate_context.raw_get());
        return result.done();
      }

      LookupResult<TreePtr<Term> > lookup(const String& name, const SourceLocation& location) {
        return lookup(name, location, TreePtr<EvaluateContext>(this));
      }
      
      template<typename V> static void visit(V& v) {visit_base<Tree>(v);}
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct EvaluateContextWrapper : NonConstructible {
      static void lookup(LookupResult<TreePtr<Term> > *result, EvaluateContext *self, const String *name, const SourceLocation *location, TreeBase* evaluate_context) {
        new (result) LookupResult<TreePtr<Term> >(Impl::lookup_impl(*static_cast<Derived*>(self), *name, *location, tree_from_base<EvaluateContext>(evaluate_context)));
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
      TreeBase* (*evaluate) (MacroEvaluateCallback*, TreeBase*, const void*, void*, TreeBase*, const SourceLocation*);
    };

    class MacroEvaluateCallback : public Tree {
    public:
      typedef MacroEvaluateCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroEvaluateCallback(const MacroEvaluateCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value, const List<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) {
        return tree_from_base_take<Term>(derived_vptr(this)->evaluate(this, value.raw_get(), parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location));
      }
    };

    template<typename Derived>
    struct MacroEvaluateCallbackWrapper : NonConstructible {
      static TreeBase* evaluate(MacroEvaluateCallback *self, TreeBase *value, const void *parameters_vtable, void *parameters_object, TreeBase *evaluate_context, const SourceLocation *location) {
        TreePtr<Term> result = Derived::evaluate_impl(*static_cast<Derived*>(self), tree_from_base<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
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
      TreeBase* (*dot) (const MacroDotCallback*, const TreeBase*, const TreeBase*, const TreeBase*, const SourceLocation*);
    };

    /**
     * \brief Wrapper class to ease using MacroEvaluateCallbackVtable from C++.
     */
    class MacroDotCallback : public Tree {
    public:
      typedef MacroDotCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroDotCallback(MacroDotCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> dot(const TreePtr<Term>& parent_value,
                        const TreePtr<Term>& child_value,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) const {
        return tree_from_base_take<Term>(derived_vptr(this)->dot(this, parent_value.raw_get(), child_value.raw_get(), evaluate_context.raw_get(), &location));
      }
    };

    /**
     * \brief Wrapper class to ease implementing MacroEvaluateCallbackVtable in C++.
     */
    template<typename Derived>
    struct MacroDotCallbackWrapper : NonConstructible {
      static TreeBase* dot(MacroDotCallback *self, TreeBase *parent_value, TreeBase *child_value, TreeBase *evaluate_context, const SourceLocation *location) {
        TreePtr<Term> result = Derived::dot_impl(*static_cast<Derived*>(self), tree_from_base<Term>(parent_value), tree_from_base<Term>(child_value), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }
    };

#define PSI_COMPILER_MACRO_DOT_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super) \
    &MacroDotCallbackWrapper<derived>::dot \
  }

    class Interface : public Tree {
    public:
      static const TreeVtable vtable;
      Interface(CompileContext& compile_context, const SourceLocation& location);
      Interface(CompileContext& compile_context, unsigned n_parameters, const SIVtable *compile_time_type, const TreePtr<Term>& run_time_type, const SourceLocation& location);

      /// \brief Number of parameters this interface takes.
      unsigned n_parameters;
      /// \brief The type that the value of this interface should extend. For run-time values this will be Term.
      SIType compile_time_type;
      /// \brief If the target of this interface is a run-time value, this gives the type of that value, otherwise it should be NULL.
      TreePtr<Term> run_time_type;
      
      template<typename Visitor> static void visit(Visitor& v);
    };
    
    struct BuiltinTypes {
      BuiltinTypes();
      void initialize(CompileContext& compile_context);

      /// \brief The empty type.
      TreePtr<Type> empty_type;
      /// \brief The bottom type.
      TreePtr<Type> bottom_type;
      /// \brief The type of types
      TreePtr<Term> metatype;
      
      /// \brief The Macro interface.
      TreePtr<Interface> macro_interface;
      /// \brief The argument passing descriptor interface.
      TreePtr<Interface> argument_passing_info_interface;
      /// \brief The class member descriptor interface.
      TreePtr<Interface> class_member_info_interface;
    };
    
    class Function;
    
    /**
     * \brief Base class for JIT compile callbacks.
     */
    class JitCompiler {
      CompileContext *m_compile_context;
      
      virtual void* build_function(const TreePtr<Function>& function) = 0;
      virtual void* build_global() = 0;
      
    public:
      JitCompiler(CompileContext *compile_context);
      virtual ~JitCompiler();
      
      CompileContext& compile_context() {return *m_compile_context;}
    };

    /**
     * \brief Context for objects used during compilation.
     *
     * This manages state which is global to the compilation and
     * compilation object lifetimes.
     */
    class CompileContext {
      friend class Object;
      friend class RunningTreeCallback;
      struct ObjectDisposer;

      std::ostream *m_error_stream;
      bool m_error_occurred;
      RunningTreeCallback *m_running_completion_stack;

      typedef boost::intrusive::list<Object, boost::intrusive::constant_time_size<false> > GCListType;
      GCListType m_gc_list;

      SourceLocation m_root_location;
      BuiltinTypes m_builtins;

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

      /// \brief Get the root location of this context.
      const SourceLocation& root_location() {return m_root_location;}
      /// \brief Get the builtin trees.
      const BuiltinTypes& builtins() {return m_builtins;}
    };

    class Block;
    class Namespace;

    TreePtr<Term> compile_expression(const SharedPtr<Parser::Expression>&, const TreePtr<EvaluateContext>&, const LogicalSourceLocationPtr&);
    TreePtr<Block> compile_statement_list(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);
    
    struct NamespaceCompileResult {
      TreePtr<Namespace> ns;
      PSI_STD::map<String, TreePtr<Term> > entries;
    };
    
    NamespaceCompileResult compile_namespace(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);

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
    TreePtr<Term> make_macro_term(CompileContext& compile_context, const SourceLocation& location, const TreePtr<Macro>& macro);
    
    TreePtr<Term> find_by_name(const TreePtr<Namespace>& ns, const std::string& name);
    TreePtr<Term> type_combine(const TreePtr<Term>& lhs, const TreePtr<Term>& rhs);
  }
}

#endif
