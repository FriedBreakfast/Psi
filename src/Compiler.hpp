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
      friend bool si_is_a(SIBase*, const SIVtable*);
      friend const SIVtable* si_vptr(SIBase*);

    protected:
      const SIVtable *m_vptr;
    };

    inline const SIVtable* si_vptr(SIBase *self) {return self->m_vptr;}
    bool si_is_a(SIBase*, const SIVtable*);

    template<typename T>
    const typename T::VtableType* derived_vptr(T *ptr) {
      return reinterpret_cast<const typename T::VtableType*>(si_vptr(ptr));
    }

    class TreeBase;
    class Tree;
    class Type;
    class CompileContext;
    template<typename> class TreePtr;

    class TreeBasePtrHook {
      template<typename> friend class TreePtr;

      void hook_reset(TreeBase *ptr, bool add_ref);
      TreeBase* hook_release() {TreeBase *tmp = m_ptr; m_ptr = NULL; return tmp;}
      Tree* hook_evaluate() const;
      void hook_update_chain(TreeBase *ptr) const;
      Tree* hook_evaluate_helper() const;

      mutable TreeBase *m_ptr;
      //TreeBase* get() const {return m_ptr;}
      
    protected:
      template<typename T> const TreePtr<T>& as_tree_ptr() const {
        return *reinterpret_cast<const TreePtr<T>*>(this);
      }
      
      template<typename T> T* ptr_as() const;
      template<typename T> T* try_ptr_as() const;

    public:
      typedef TreeBase GetType;

      TreeBasePtrHook() : m_ptr(NULL) {}
      TreeBasePtrHook(const TreeBasePtrHook& src) : m_ptr(NULL) {hook_reset(src.m_ptr, true);}
      ~TreeBasePtrHook() {hook_reset(NULL, false);}
      TreeBasePtrHook& operator = (const TreeBasePtrHook& src) {hook_reset(src.m_ptr, true); return *this;}
      
      CompileContext& compile_context() const;
      const SourceLocation& location() const;
    };

    /**
     * Wrapper class to avoid referencing ::PtrHookType inside TreePtr, to avoid
     * template instantiation issues.
     */
    template<typename T>
    class TreePtrHookWrapper {
      typedef typename T::PtrHook PtrHookType;
      const TreeBasePtrHook *m_hook;

    public:
      TreePtrHookWrapper(const TreeBasePtrHook *hook) : m_hook(hook) {}
      const PtrHookType* operator -> () const {return static_cast<const PtrHookType*>(m_hook);}
    };

    template<typename T=Tree>
    class TreePtr {
      template<typename> friend class TreePtr;
      friend class TreeBasePtrHook;
      
      typedef void (TreePtr::*safe_bool_type) () const;
      void safe_bool_true() const {}
      
      template<typename U> friend TreePtr<U> tree_from_base(TreeBase*, bool);
      TreePtr(TreeBase *src, bool add_ref) {m_hook.hook_reset(src, add_ref);}

      TreeBasePtrHook m_hook;

    public:
      TreePtr() {}
      explicit TreePtr(T *ptr) {m_hook.hook_reset(ptr, true);}
      TreePtr(T *ptr, bool add_ref) {m_hook.hook_reset(ptr, add_ref);}
      template<typename U> TreePtr(const TreePtr<U>& src) {BOOST_STATIC_ASSERT((boost::is_convertible<U*, T*>::value)); m_hook = src.m_hook;}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {BOOST_STATIC_ASSERT((boost::is_convertible<U*, T*>::value)); m_hook = src.m_hook; return *this;}
      void reset(T *ptr=0, bool add_ref=true) {m_hook.hook_reset(ptr, add_ref);}

      T* get() const {return m_hook.ptr_as<T>();}
      const TreePtrHookWrapper<T> operator -> () const {return TreePtrHookWrapper<T>(&m_hook);}

      TreeBase *raw_get() const {return m_hook.m_ptr;}
      TreeBase* release() {return m_hook.hook_release();}
      
      operator safe_bool_type () const {return get() ? &TreePtr::safe_bool_true : 0;}
      bool operator ! () const {return !get();}
      template<typename U> bool operator == (const TreePtr<U>& other) const {return get() == other.get();};
      template<typename U> bool operator != (const TreePtr<U>& other) const {return get() != other.get();};
      template<typename U> bool operator < (const TreePtr<U>& other) const {return get() < other.get();};
    };

    /**
     * Get a TreePtr from a point to a TreeBase.
     *
     * This should only be used in wrapper functions, since otherwise the type of \c base
     * should be statically known.
     */
    template<typename T>
    TreePtr<T> tree_from_base(TreeBase *base, bool add_ref=true) {
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
      friend class TreeBasePtrHook;
      friend class TreeCallback;
      friend class GCVisitorIncrement;
      friend class GCVisitorDecrement;
      friend class GCVisitorClear;

      std::size_t m_reference_count;
      CompileContext *m_compile_context;
      SourceLocation m_location;

    protected:
      CompileContext& compile_context() const {return *m_compile_context;}
      const SourceLocation& location() const {return m_location;}

    public:
      typedef TreeBasePtrHook PtrHook;
      typedef TreeBaseVtable VtableType;
      static const SIVtable vtable;
      
      TreeBase(CompileContext& compile_context, const SourceLocation& location);
      ~TreeBase();

      template<typename Visitor>
      static void visit_impl(TreeBase& self PSI_ATTRIBUTE((PSI_UNUSED)), Visitor& visitor PSI_ATTRIBUTE((PSI_UNUSED))) {}
    };

    /// \brief Get the compile context for this Tree, without evaluating the Tree.
    inline CompileContext& TreeBasePtrHook::compile_context() const {return *m_ptr->m_compile_context;}
    /// \brief Get the location of this Tree, without evaluating the Tree.
    inline const SourceLocation& TreeBasePtrHook::location() const {return m_ptr->m_location;}

    /**
     * \brief Base classes for gargabe collection phase
     * implementations.
     */
    template<typename Derived>
    class VisitorBase {
      Derived& derived() {
      	return *static_cast<Derived*>(this);
      }

    public:
      template<typename T>
      void visit_collection (T& collection) {
	      for (typename T::iterator ii = collection.begin(), ie = collection.end(); ii != ie; ++ii)
	        operator () (NULL, *ii);
      }

      template<typename T>
      Derived& operator () (const char*, PSI_STD::vector<T>& obj) {
	      derived().visit_collection(obj);
	      return derived();
      }

      template<typename T, typename U>
      Derived& operator () (const char*, PSI_STD::map<T, U>& obj) {
	      derived().visit_collection(obj);
	      return derived();
      }

      template<typename T, typename U>
      Derived& operator () (const char*, PSI_STD::pair<T, U>& obj) {
	      operator () (NULL, obj.first);
	      operator () (NULL, obj.second);
	      return derived();
      }

      Derived& operator () (const char*, String&) {return derived();}
      Derived& operator () (const char*, const String&) {return derived();}
      template<typename T> Derived& operator () (const char*, const SharedPtr<T>&) {return derived();}
      Derived& operator () (const char*, unsigned) {return derived();}

      template<typename T>
      Derived& operator () (const char*, TreePtr<T>& ptr) {
	      derived().visit_tree_ptr(ptr);
	      return derived();
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorIncrement : public VisitorBase<GCVisitorIncrement> {
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
    class GCVisitorDecrement : public VisitorBase<GCVisitorDecrement> {
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
    class GCVisitorClear : public VisitorBase<GCVisitorClear> {
    public:
      template<typename T>
      void visit_collection(T& collection) {
        collection.clear();
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
        GCVisitorIncrement p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_decrement(TreeBase *self) {
        GCVisitorDecrement p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_clear(TreeBase *self) {
        GCVisitorClear p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
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
    };

    class Tree : public TreeBase {
    public:
      static const SIVtable vtable;
      typedef TreeVtable VtableType;

      Tree(CompileContext&, const SourceLocation&);

      class PtrHook : public TreeBase::PtrHook {
        Tree* get() const {return ptr_as<Tree>();}

      public:
        void complete() const {
          Tree *self = get();
          derived_vptr(self)->complete(self);
        }
      };
    };

    template<typename T> T* TreeBasePtrHook::ptr_as() const {
      Tree *t = hook_evaluate();
      PSI_ASSERT(!t || si_is_a(t, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<T*>(t);
    };

    template<typename T> T* TreeBasePtrHook::try_ptr_as() const {
      if (!m_ptr || derived_vptr(m_ptr)->is_callback)
        return NULL;

      PSI_ASSERT(si_is_a(m_ptr, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<T*>(m_ptr);
    }

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
    class CompleteVisitor : public VisitorBase<CompleteVisitor> {
    public:
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        if (ptr)
          ptr->complete();
      }
    };

    template<typename Derived>
    struct TreeWrapper : NonConstructible {
      static void complete(Tree *self) {
        CompleteVisitor p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_TREE_BASE(false,derived,name,super), \
    &::Psi::Compiler::TreeWrapper<derived>::complete \
  }

#define PSI_COMPILER_TREE_INIT() (PSI_REQUIRE_CONVERTIBLE(&vtable, const VtableType*), PSI_COMPILER_SI_INIT(&vtable))
#define PSI_COMPILER_TREE_ABSTRACT(name,super) PSI_COMPILER_SI_ABSTRACT(name,&super::vtable)

    inline Tree* TreeBasePtrHook::hook_evaluate() const {
      return (!m_ptr || !derived_vptr(m_ptr)->is_callback) ? static_cast<Tree*>(m_ptr) : hook_evaluate_helper();
    }

    inline void TreeBasePtrHook::hook_reset(TreeBase *ptr, bool add_ref) {
      if (ptr && add_ref)
        ++ptr->m_reference_count;

      if (m_ptr)
        if (!--m_ptr->m_reference_count)
          derived_vptr(m_ptr)->destroy(m_ptr);

      m_ptr = ptr;

      PSI_ASSERT(!m_ptr || derived_vptr(m_ptr));
    }

    /// \see TreeCallback
    struct TreeCallbackVtable {
      TreeBaseVtable base;
      TreeBase* (*evaluate) (TreeCallback*);
    };

    class TreeCallback : public TreeBase {
      friend class TreeBasePtrHook;

    public:
      enum CallbackState {
        state_ready,
        state_running,
        state_finished,
        state_failed
      };

      CallbackState m_state;
      TreePtr<> m_value;

    protected:
      CompileContext& compile_context() const {return *m_compile_context;}
      const SourceLocation& location() const {return m_location;}

    public:
      static const SIVtable vtable;

      TreeCallback(CompileContext&, const SourceLocation&);

      template<typename Visitor> static void visit_impl(TreeCallback& self, Visitor& visitor) {
        TreeBase::visit_impl(self, visitor);
        visitor("value", self.m_value);
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
      static void visit_impl(TreeCallbackImpl& self, Visitor& visitor) {
        TreeCallback::visit_impl(self, visitor);
        if (self.m_function)
          self.m_function->visit(visitor);
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

    class Term;
    class Interface;

    struct TermVtable {
      TreeVtable base;
      PsiBool (*match) (Term*,Term*,const void*,void*,unsigned);
      TreeBase* (*rewrite) (Term*,const SourceLocation*, const void*,void*);
      TreeBase* (*interface_search) (Tree*, Interface*, const void*, void*);
    };

    class Term : public Tree {
      friend class Metatype;
      class TypeGetter;
      Term(CompileContext&, const SourceLocation&);

      TreePtr<Term> m_type;

      TreePtr<Term> rewrite(const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);
      bool match(const TreePtr<Term>&, const List<TreePtr<Term> >&, unsigned);

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;
      
      static const SIVtable vtable;

      Term(const TreePtr<Term>&, const SourceLocation&);

      class PtrHook : public Tree::PtrHook {
        Term *get() const {return ptr_as<Term>();}

      public:
        /// \brief Get the type of this tree
        TreePtr<Term> type() const {return get()->m_type;}
        TreePtr<Term> rewrite(const SourceLocation& location, const Map<TreePtr<Term>, TreePtr<Term> >& substitutions) const {return get()->rewrite(location, substitutions);}
        bool match(const TreePtr<Term>& value, const List<TreePtr<Term> >& wildcards, unsigned depth=0) const {return get()->match(value, wildcards, depth);}

        TreePtr<> interface_search(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) const {
          Term *self = get();
          return tree_from_base<Tree>(derived_vptr(self)->interface_search(self, interface.get(), parameters.vptr(), parameters.object()), false);
        }
      };

      template<typename Visitor> static void visit_impl(Term& self, Visitor& visitor) {
        Tree::visit_impl(self, visitor);
        visitor("type", self.m_type);
      }

      static TreePtr<> interface_search_impl(Term& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
      static bool match_impl(Term&, Term&, const List<TreePtr<Term> >&, unsigned);
      static TreePtr<Term> rewrite_impl(Term&, const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);
    };

    template<typename Derived>
    struct TermWrapper : NonConstructible {
      static PsiBool match(Term *left, Term *right, const void *wildcards_vtable, void *wildcards_obj, unsigned depth) {
        return Derived::match_impl(*static_cast<Derived*>(left), *static_cast<Derived*>(right), List<TreePtr<Term> >(wildcards_vtable, wildcards_obj), depth);
      }

      static TreeBase* rewrite(Term *self, const SourceLocation *location, const void *substitutions_vtable, void *substitutions_obj) {
        TreePtr<Term> result = Derived::rewrite_impl(*static_cast<Derived*>(self), *location, Map<TreePtr<Term>, TreePtr<Term> >(substitutions_vtable, substitutions_obj));
        return result.release();
      }

      static TreeBase* interface_search(Tree *self, Interface *interface, const void *parameters_vtable, void *parameters_object) {
        TreePtr<> result = Derived::interface_search_impl(*static_cast<Derived*>(self), TreePtr<Interface>(interface), List<TreePtr<Term> >(parameters_vtable, parameters_object));
        return result.release();
      }
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TermWrapper<derived>::match, \
    &::Psi::Compiler::TermWrapper<derived>::rewrite, \
    &::Psi::Compiler::TermWrapper<derived>::interface_search \
  }

    class Type : public Term {
    public:
      static const SIVtable vtable;
      Type(CompileContext&, const SourceLocation&);
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_TERM(derived,name,super)

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
      TreeBase* (*evaluate) (Macro*, Term*, const void*, void*, EvaluateContext*, const SourceLocation*);
      TreeBase* (*dot) (Macro*, Term*, const SharedPtr<Parser::Expression>*,  EvaluateContext*, const SourceLocation*);
    };

    class Macro : public Tree {
    public:
      typedef MacroVtable VtableType;
      static const SIVtable vtable;

      Macro(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      struct PtrHook : Tree::PtrHook {
        TreePtr<Term> evaluate(const TreePtr<Term>& value,
                               const List<SharedPtr<Parser::Expression> >& parameters,
                               const TreePtr<EvaluateContext>& evaluate_context,
                               const SourceLocation& location) const {
          Macro *self = ptr_as<Macro>();
          return tree_from_base<Term>(derived_vptr(self)->evaluate(self, value.get(), parameters.vptr(), parameters.object(), evaluate_context.get(), &location), false);
        }

        TreePtr<Term> dot(const TreePtr<Term>& value,
                          const SharedPtr<Parser::Expression>& parameter,
                          const TreePtr<EvaluateContext>& evaluate_context,
                          const SourceLocation& location) const {
          Macro *self = ptr_as<Macro>();
          return tree_from_base<Term>(derived_vptr(self)->dot(self, value.get(), &parameter, evaluate_context.get(), &location), false);
        }
      };
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static TreeBase* evaluate(Macro *self,
                                Term *value,
                                const void *parameters_vtable,
                                void *parameters_object,
                                EvaluateContext *evaluate_context,
                                const SourceLocation *location) {
        TreePtr<Term> result = Impl::evaluate_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), TreePtr<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }

      static TreeBase* dot(Macro *self,
                           Term *value,
                           const SharedPtr<Parser::Expression> *parameter,
                           EvaluateContext *evaluate_context,
                           const SourceLocation *location) {
        TreePtr<Term> result = Impl::dot_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), *parameter, TreePtr<EvaluateContext>(evaluate_context), *location);
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
      void (*lookup) (LookupResult<TreePtr<Term> >*, EvaluateContext*, const String*);
    };

    class EvaluateContext : public Tree {
    public:
      typedef EvaluateContextVtable VtableType;
      static const SIVtable vtable;

      EvaluateContext(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      class PtrHook : public Tree::PtrHook {
      public:
        LookupResult<TreePtr<Term> > lookup(const String& name) const {
          ResultStorage<LookupResult<TreePtr<Term> > > result;
          EvaluateContext *self = ptr_as<EvaluateContext>();
          derived_vptr(self)->lookup(result.ptr(), self, &name);
          return result.done();
        }
      };
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct EvaluateContextWrapper : NonConstructible {
      static void lookup(LookupResult<TreePtr<Term> > *result, EvaluateContext *self, const String *name) {
        new (result) LookupResult<TreePtr<Term> >(Impl::lookup_impl(*static_cast<Derived*>(self), *name));
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
      TreeBase* (*evaluate) (MacroEvaluateCallback*, Term*, const void*, void*, EvaluateContext*, const SourceLocation*);
    };

    class MacroEvaluateCallback : public Tree {
    public:
      typedef MacroEvaluateCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroEvaluateCallback(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      class PtrHook : public Tree::PtrHook {
      public:
        TreePtr<Term> evaluate(const TreePtr<Term>& value, const List<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) const {
          MacroEvaluateCallback *self = ptr_as<MacroEvaluateCallback>();
          return tree_from_base<Term>(derived_vptr(self)->evaluate(self, value.get(), parameters.vptr(), parameters.object(), evaluate_context.get(), &location), false);
        }
      };
    };

    template<typename Derived>
    struct MacroEvaluateCallbackWrapper : NonConstructible {
      static TreeBase* evaluate(MacroEvaluateCallback *self, Term *value, const void *parameters_vtable, void *parameters_object, EvaluateContext *evaluate_context, const SourceLocation *location) {
        TreePtr<Term> result = Derived::evaluate_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), TreePtr<EvaluateContext>(evaluate_context), *location);
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
      Term* (*dot) (MacroDotCallback*, Term*, EvaluateContext*, const SourceLocation*);
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

      class PtrHook : public Tree::PtrHook {
      public:
        TreePtr<Term> dot(const TreePtr<Term>& value,
                          const TreePtr<EvaluateContext>& evaluate_context,
                          const SourceLocation& location) const {
          MacroDotCallback *self = ptr_as<MacroDotCallback>();
          return TreePtr<Term>(derived_vptr(self)->dot(self, value.get(), evaluate_context.get(), &location), false);
        }
      };
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
      static void visit_impl(Interface& self, Visitor& visitor) {
	      Tree::visit_impl(self, visitor);
	      visitor
	        ("run_time_type", self.m_run_time_type);
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
    TreePtr<Block> compile_statement_list(const List<SharedPtr<Parser::NamedExpression> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);

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
