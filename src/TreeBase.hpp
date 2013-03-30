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
    class Tree;
    class CompileContext;
    
    template<typename T=Tree>
    class TreePtr : public ObjectPtr<const T> {
    public:
      TreePtr() {}
      explicit TreePtr(const T *ptr) : ObjectPtr<const T>(ptr) {}
      template<typename U> TreePtr(const TreePtr<U>& src) : ObjectPtr<const T>(src) {}

      /// \brief Get the location of this tree
      const SourceLocation& location() const {return this->get()->location();}
    };
    
    template<typename T> std::size_t hash_value(const TreePtr<T>& ptr) {return boost::hash_value(ptr.get());}
    template<typename T> TreePtr<T> tree_from(const T *ptr) {return TreePtr<T>(ptr);}

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
    
    /**
     * \brief Recursively completes a tree.
     */
    class CompleteVisitor : public ObjectVisitorBase<CompleteVisitor> {
      VisitQueue<TreePtr<> > *m_queue;
      
    public:
      CompleteVisitor(VisitQueue<TreePtr<> > *queue) : m_queue(queue) {
      }
      
      template<typename T>
      void visit_object_ptr(const ObjectPtr<T>&) {}
      
      template<typename T>
      void visit_tree_ptr(const TreePtr<T>& ptr) {
        if (ptr)
          m_queue->push(ptr);
      }
      
      template<typename T, typename U>
      void visit_delayed(DelayedValue<T,U>& ptr) {
        boost::array<T*,1> m = {{const_cast<T*>(&ptr.get_checked())}};
        visit_callback(*this, NULL, m);
      }
    };


    /// \see Tree
    struct TreeVtable {
      ObjectVtable base;
      void (*complete) (const Tree*,VisitQueue<TreePtr<> >*);
    };

    class Tree : public Object {
      friend class CompileContext;
      friend class Term;

      SourceLocation m_location;

      /// Disable general new operator
      static void* operator new (size_t) {PSI_FAIL("Tree::new should never be called");}
      /// Disable placement new
      static void* operator new (size_t, void*) {PSI_FAIL("Tree::new should never be called");}

      Tree(const TreeVtable *vptr);

    public:
      PSI_COMPILER_EXPORT static const SIVtable vtable;
      
      typedef TreeVtable VtableType;

      Tree(const TreeVtable *vptr, CompileContext& compile_context, const SourceLocation& location);

      PSI_COMPILER_EXPORT void complete() const;
      const SourceLocation& location() const {return m_location;}
      
      template<typename V> static void visit(V& PSI_UNUSED(v)) {}

#if PSI_DEBUG
      void debug_print() const;
#endif
      
      template<typename Derived>
      static void complete_impl(const Derived& self, VisitQueue<TreePtr<> >& queue) {
        Derived::local_complete_impl(self);
        boost::array<Derived*, 1> a = {{const_cast<Derived*>(&self)}};
        CompleteVisitor p(&queue);
        visit_members(p, a);
      }
      
      static void local_complete_impl(const Tree&) {};
    };

    template<typename T>
    bool tree_isa(const Tree *ptr) {
      return ptr && si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable));
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
    bool tree_isa(const TreePtr<U>& ptr) {
      return tree_isa<T>(ptr.get());
    }

    template<typename T, typename U>
    TreePtr<T> treeptr_cast(const TreePtr<U>& ptr) {
      PSI_ASSERT(tree_isa<T>(ptr));
      return TreePtr<T>(static_cast<const T*>(ptr.get()));
    }

    template<typename T, typename U>
    TreePtr<T> dyn_treeptr_cast(const TreePtr<U>& ptr) {
      return tree_isa<T>(ptr) ? TreePtr<T>(static_cast<const T*>(ptr.get())) : TreePtr<T>();
    }

    template<typename Derived>
    struct TreeWrapper : NonConstructible {
      static void complete(const Tree *self, VisitQueue<TreePtr<> > *queue) {
        Derived::complete_impl(*static_cast<const Derived*>(self), *queue);
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_OBJECT(derived,name,super), \
    &::Psi::Compiler::TreeWrapper<derived>::complete \
  }

#define PSI_COMPILER_TREE_ABSTRACT(name,super) PSI_COMPILER_SI_ABSTRACT(name,&super::vtable)

    class DelayedEvaluation;
    
    struct DelayedEvaluationVtable {
      ObjectVtable base;
      void (*evaluate) (void *result, DelayedEvaluation *self, void *arg);
    };

    class DelayedEvaluation : public Object {
    public:
      enum CallbackState {
        state_ready,
        state_running,
        state_finished,
        state_failed
      };

    private:
      SourceLocation m_location;
      CallbackState m_state;
      
    protected:
      void evaluate(void *ptr, void *arg);
      
    public:
      typedef DelayedEvaluationVtable VtableType;
      DelayedEvaluation(const DelayedEvaluationVtable *vptr, CompileContext& compile_context, const SourceLocation& location);
      const SourceLocation& location() const {return m_location;}
      
      bool running() const {return m_state == state_running;}

      template<typename V>
      static void visit(V& v) {
        v("location", &DelayedEvaluation::m_location);
      }
    };

    /**
     * \brief Data for a running TreeCallback.
     */
    class RunningTreeCallback : public boost::noncopyable {
      DelayedEvaluation *m_callback;
      RunningTreeCallback *m_parent;

    public:
      RunningTreeCallback(DelayedEvaluation *callback);
      ~RunningTreeCallback();

      PSI_ATTRIBUTE((PSI_NORETURN)) void throw_circular_dependency();
    };
    
    template<typename DataType_, typename ArgumentType_>
    struct DelayedEvaluationArgs {
      typedef DataType_ DataType;
      typedef ArgumentType_ ArgumentType;
    };
    
    template<typename Args>
    class DelayedEvaluationCallback : public DelayedEvaluation {
    public:
      typedef typename Args::DataType DataType;
      typedef typename Args::ArgumentType ArgumentType;

      static const SIVtable vtable;

      DelayedEvaluationCallback(const DelayedEvaluationVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : DelayedEvaluation(vptr, compile_context, location) {}
      
      DataType evaluate(const ArgumentType& arg) {
        ResultStorage<DataType> rs;
        DelayedEvaluation::evaluate(rs.ptr(), const_cast<ArgumentType*>(&arg));
        return rs.done();
      }

      template<typename V>
      static void visit(V& v) {
        visit_base<DelayedEvaluation>(v);
      }
    };
    
    template<typename T>
    const SIVtable DelayedEvaluationCallback<T>::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.DelayedEvaluationCallback", &DelayedEvaluation::vtable);
    
    template<typename BaseArgs_, typename FunctionType_>
    struct DelayedEvaluationImplArgs {
      typedef BaseArgs_ BaseArgs;
      typedef FunctionType_ FunctionType;
    };

    template<typename Args>
    class DelayedEvaluationImpl : public DelayedEvaluationCallback<typename Args::BaseArgs> {
    public:
      typedef typename Args::BaseArgs BaseArgs;
      typedef typename BaseArgs::DataType DataType;
      typedef typename BaseArgs::ArgumentType ArgumentType;
      typedef typename Args::FunctionType FunctionType;
      
    private:
      FunctionType *m_function;
      
    public:
      static const DelayedEvaluationVtable vtable;

      DelayedEvaluationImpl(CompileContext& compile_context, const SourceLocation& location, const FunctionType& function)
      : DelayedEvaluationCallback<BaseArgs>(&vtable, compile_context, location),
      m_function(new FunctionType(function)) {
      }

      ~DelayedEvaluationImpl() {
        if (m_function)
          delete m_function;
      }

      static void evaluate_impl(void *result, DelayedEvaluation *self_ptr, void *arg) {
        DelayedEvaluationImpl<Args>& self = *static_cast<DelayedEvaluationImpl<Args>*>(self_ptr);
        boost::scoped_ptr<FunctionType> function_copy(self.m_function);
        self.m_function = NULL;
        new (result) DataType (function_copy->evaluate(*static_cast<ArgumentType*>(arg)));
      }

      template<typename V>
      static void visit(V& v) {
        visit_base<DelayedEvaluationCallback<BaseArgs> >(v);
        v("function", &DelayedEvaluationImpl::m_function);
      }
    };
    
#define PSI_COMPILER_DELAYED_EVALUATION(derived,name,super) { \
    PSI_COMPILER_OBJECT(derived,name,super), \
    &derived::evaluate_impl \
  }

#if !PSI_DEBUG
    template<typename T>
    const DelayedEvaluationVtable DelayedEvaluationImpl<T>::vtable = PSI_COMPILER_DELAYED_EVALUATION(DelayedEvaluationImpl<T>, "(callback)", DelayedEvaluationCallback<typename T::BaseArgs>);
#else
    template<typename T>
    const DelayedEvaluationVtable DelayedEvaluationImpl<T>::vtable = PSI_COMPILER_DELAYED_EVALUATION(DelayedEvaluationImpl<T>, typeid(typename T::FunctionType).name(), DelayedEvaluationCallback<typename T::BaseArgs>);
#endif
    
    /**
     * \brief A value filled in on demand by a callback.
     * 
     * This should be movable since the callback may only be used once,
     * but I'm not using C++11 so copyable it is.
     */
    template<typename T, typename Arg>
    class DelayedValue {
      mutable T m_value;
      typedef DelayedEvaluationArgs<T, Arg> BaseArgs;
      mutable ObjectPtr<DelayedEvaluationCallback<BaseArgs> > m_callback;
      
      template<typename U>
      void init(boost::true_type, CompileContext&, const SourceLocation&, const U& value) {
        m_value = value;
      }

      template<typename U>
      void init(boost::false_type, CompileContext& compile_context, const SourceLocation& location, const U& callback) {
        m_callback.reset(new DelayedEvaluationImpl<DelayedEvaluationImplArgs<BaseArgs, U> >(compile_context, location, callback));
      }
      
    public:
      template<typename U>
      DelayedValue(CompileContext& compile_context, const SourceLocation& location, const U& callback_or_value) {
        init(boost::is_convertible<U,T>(), compile_context, location, callback_or_value);
      }
      
      template<typename X, typename Y>
      const T& get(const X *self, Y (X::*getter) () const, void (X::*checker) (T&) const = NULL) const {
        if (m_callback) {
          m_value = m_callback->evaluate((self->*getter)());
          m_callback.reset();
          if (checker)
            (self->*checker)(m_value);
        }
        return m_value;
      }
      
      /// \brief Get the value if it has already been built
      T* get_maybe() {
        return m_callback ? NULL : &m_value;
      }
      
      /// \brief Get a value which must have already been computed
      const T& get_checked() const {
        PSI_ASSERT(!m_callback);
        return m_value;
      }
      
      /// \brief True if the callback is currently executing
      bool running() const {
        return m_callback && m_callback->running();
      }
      
      template<typename V>
      static void visit(V& v) {
        v("value", &DelayedValue::m_value)
        ("callback", &DelayedValue::m_callback);
      }
    };
  }
}

#endif