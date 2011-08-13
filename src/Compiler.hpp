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

    class Tree;
    class Type;
    class CompileContext;

    template<typename T=Tree>
    class TreePtr : public IntrusivePointer<T> {
    public:
      TreePtr() {}
      explicit TreePtr(T *ptr) : IntrusivePointer<T>(ptr) {}
      TreePtr(T *ptr, bool add_ref) : IntrusivePointer<T>(ptr, add_ref) {}
      template<typename U> TreePtr(const TreePtr<U>& src) : IntrusivePointer<T>(src) {}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {this->reset(src.get()); return *this;}
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

    class VisitorPlaceholder {
    public:
      template<typename T>
      VisitorPlaceholder& operator () (const char */*name*/, T& /*member*/) {
        return *this;
      }
    };

    struct Dependency;

    struct DependencyVtable {
      SIVtable base;
      void (*run) (Dependency*, Tree*);
      void (*gc_increment) (Dependency*);
      void (*gc_decrement) (Dependency*);
      void (*destroy) (Dependency*);
    };

    class Dependency : public SIBase {
    public:
      typedef DependencyVtable VtableType;

      void run(Tree *tree) {
        derived_vptr(this)->run(this, tree);
      }

      void gc_increment() {
	derived_vptr(this)->gc_increment(this);
      }

      void gc_decrement() {
	derived_vptr(this)->gc_decrement(this);
      }

      void destroy() {
        derived_vptr(this)->destroy(this);
      }

      template<typename Visitor> static void visit_impl(Dependency&, Visitor&) {}
    };

    class DependencyPtr : public PointerBase<Dependency> {
    public:
      DependencyPtr() {}
      explicit DependencyPtr(Dependency *ptr) : PointerBase<Dependency>(ptr) {}
      ~DependencyPtr() {clear();}
      void clear() {if (m_ptr) {m_ptr->destroy(); m_ptr = 0;}}
      void swap(DependencyPtr& src) {std::swap(m_ptr, src.m_ptr);}
      void reset(Dependency *ptr) {clear(); m_ptr = ptr;}
    };

    class CompletionState : boost::noncopyable {
      enum State {
        completion_constructed,
        completion_running,
        completion_finished,
        completion_failed
      };

      char m_state;
      
      template<typename A, typename B>
      void complete_main(const A& body, const B& cleanup) {
        try {
          m_state = completion_running;
          body();
          m_state = completion_finished;
        } catch (...) {
          m_state = completion_failed;
          cleanup();
          throw CompileException();
        }
        cleanup();
      }

      struct EmptyCallback {void operator () () const {}};

    public:
      CompletionState() : m_state(completion_constructed) {}

      template<typename A, typename B>
      void complete(CompileContext&, const SourceLocation&, bool, const A&, const B&);

      template<typename A>
      void complete(CompileContext& compile_context, const SourceLocation& location, bool dependency, const A& body) {
        complete(compile_context, location, dependency, body, EmptyCallback());
      }
    };

    struct TreeVtable {
      SIVtable base;
      void (*destroy) (Tree*);
      void (*gc_increment) (Tree*);
      void (*gc_decrement) (Tree*);
      void (*gc_clear) (Tree*);
      void (*complete_callback) (Tree*);
      void (*complete_cleanup) (Tree*);
    };

    class Tree : public SIBase, public boost::intrusive::list_base_hook<> {
      friend class CompileContext;
      template<typename> friend class TreePtr;
      friend class GCVisitorIncrement;
      friend class GCVisitorDecrement;
      friend class GCVisitorClear;

      std::size_t m_reference_count;

      CompileContext *m_compile_context;
      SourceLocation m_location;
      CompletionState m_completion_state;

      void destroy() {derived_vptr(this)->destroy(this);}
      void gc_increment() {derived_vptr(this)->gc_increment(this);}
      void gc_decrement() {derived_vptr(this)->gc_decrement(this);}
      void gc_clear() {derived_vptr(this)->gc_clear(this);}

    public:
      static const SIVtable vtable;
      typedef TreeVtable VtableType;

      Tree(CompileContext&, const SourceLocation&);
      ~Tree();

      /// \brief Return the compilation context this tree belongs to.
      CompileContext& compile_context() const {return *m_compile_context;}
      /// \brief Get the location associated with this tree
      const SourceLocation& location() const {return m_location;}

      void complete(bool=false);

      template<typename Visitor> static void visit_impl(Tree&, Visitor&) {}
      static void complete_callback_impl(Tree&) {}
      static void complete_cleanup_impl(Tree&) {}

      friend void intrusive_ptr_add_ref(Tree *self) {
	++self->m_reference_count;
      }

      friend void intrusive_ptr_release(Tree *self) {
	if (!--self->m_reference_count)
	  self->destroy();
      }
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
     * \brief Base classes for gargabe collection phase
     * implementations.
     */
    template<typename Derived>
    class GCVisitorBase {
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
      Derived& operator () (const char*, const TreeVtable*) {return derived();}
      Derived& operator () (const char*, unsigned) {return derived();}

      template<typename T>
      Derived& operator () (const char*, TreePtr<T>& ptr) {
	derived().visit_tree_ptr(ptr);
	return derived();
      }

      Derived& operator () (const char*, DependencyPtr& ptr) {
	derived().visit_dependency_ptr(ptr);
	return derived();
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorIncrement : public GCVisitorBase<GCVisitorIncrement> {
    public:
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
	if (ptr)
	  ++ptr->m_reference_count;
      }

      void visit_dependency_ptr(DependencyPtr& ptr) {
	ptr->gc_increment();
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorDecrement : public GCVisitorBase<GCVisitorDecrement> {
    public:
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
	if (ptr)
	  --ptr->m_reference_count;
      }

      void visit_dependency_ptr(DependencyPtr& ptr) {
	ptr->gc_decrement();
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorClear : public GCVisitorBase<GCVisitorClear> {
    public:
      template<typename T>
      void visit_collection(T& collection) {
	collection.clear();
      }

      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
	ptr.reset();
      }

      void visit_dependency_ptr(DependencyPtr& ptr) {
	ptr.clear();
      }
    };

    template<typename Derived>
    struct TreeWrapper {
      static void destroy(Tree *self) {
        delete static_cast<Derived*>(self);
      }

      static void gc_increment(Tree *self) {
	GCVisitorIncrement p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_decrement(Tree *self) {
	GCVisitorDecrement p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_clear(Tree *self) {
	GCVisitorClear p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void complete_callback(Tree *self) {
        Derived::complete_callback_impl(*static_cast<Derived*>(self));
      }

      static void complete_cleanup(Tree *self) {
        Derived::complete_cleanup_impl(*static_cast<Derived*>(self));
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_SI(name,&super::vtable), \
    &TreeWrapper<derived>::destroy, \
    &TreeWrapper<derived>::gc_increment, \
    &TreeWrapper<derived>::gc_decrement, \
    &TreeWrapper<derived>::gc_clear, \
    &TreeWrapper<derived>::complete_callback, \
    &TreeWrapper<derived>::complete_cleanup \
  }

#define PSI_COMPILER_TREE_INIT() (PSI_REQUIRE_CONVERTIBLE(&vtable, const VtableType*), PSI_COMPILER_SI_INIT(&vtable))
#define PSI_COMPILER_TREE_ABSTRACT(name,super) PSI_COMPILER_SI_ABSTRACT(name,&super::vtable)

    /**
     * \brief Base class to simplify implementing Dependency in C++.
     */
    template<typename Derived, typename TreeType>
    struct DependencyWrapper : NonConstructible {
      static void run(Dependency *self, Tree *target) {
        Derived::run_impl(*static_cast<Derived*>(self), TreePtr<TreeType>(tree_cast<TreeType>(target)));
      }

      static void gc_increment(Dependency *self) {
        GCVisitorIncrement p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_decrement(Dependency *self) {
        GCVisitorDecrement p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void destroy(Dependency *self) {
        delete static_cast<Derived*>(self);
      }
    };

#define PSI_COMPILER_DEPENDENCY(derived,name,tree) { \
    PSI_COMPILER_SI(name,NULL), \
    &DependencyWrapper<derived, tree>::run, \
    &DependencyWrapper<derived, tree>::gc_increment, \
    &DependencyWrapper<derived, tree>::gc_decrement, \
    &DependencyWrapper<derived, tree>::destroy \
  }

#define PSI_COMPILER_DEPENDENCY_INIT() PSI_COMPILER_SI_INIT(&vtable)

    class Term;

    struct TermVtable {
      TreeVtable base;
      PsiBool (*match) (Term*,Term*,const void*,void*,unsigned);
      Term* (*rewrite) (Term*,const SourceLocation*, const void*,void*);
      void (*iterate) (void*,Term*);
      IteratorVtable iterator_vtable;
    };

    class Term : public Tree {
      friend class Metatype;
      Term(CompileContext&, const SourceLocation&);

    protected:
      TreePtr<Term> m_type;

    public:
      typedef TermVtable VtableType;
      typedef TreePtr<Term> IteratorValueType;
      
      static const SIVtable vtable;

      Term(const TreePtr<Term>&, const SourceLocation&);
      ~Term();

      /// \brief Get the type of this tree
      const TreePtr<Term>& type() const {return m_type;}
      
      TreePtr<Term> rewrite(const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);
      bool match(const TreePtr<Term>&, const List<TreePtr<Term> >&);
      bool match(const TreePtr<Term>&, const List<TreePtr<Term> >&, unsigned);

      friend const IteratorVtable* iterator_vptr(Term& self) {
        return &derived_vptr(&self)->iterator_vtable;
      }

      friend void iterator_init(void *dest, Term& self) {
        derived_vptr(&self)->iterate(dest, &self);
      }

      template<typename Visitor> static void visit_impl(Term& self, Visitor& visitor) {
	Tree::visit_impl(self, visitor);
	visitor("type", self.m_type);
      }

      static bool match_impl(Term&, Term&, const List<TreePtr<Term> >&, unsigned);
      static TreePtr<Term> rewrite_impl(Term&, const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);

      class IteratorType {
	bool m_done;
        TreePtr<Term> m_type;
        
      public:
        IteratorType(const TreePtr<Term>& self) : m_done(!self->m_type), m_type(self->m_type) {}
        TreePtr<Term>& current() {return m_type;}
        bool next() {if (m_done) {return false;} else {m_done = true; return true;}}
        void move_from(IteratorType& src) {std::swap(*this, src);}
      };
    };

    template<typename Derived>
    struct TermWrapper : NonConstructible {
      static PsiBool match(Term *left, Term *right, const void *wildcards_vtable, void *wildcards_obj, unsigned depth) {
        return Derived::match_impl(*tree_cast<Derived>(left), *tree_cast<Derived>(right), List<TreePtr<Term> >(wildcards_vtable, wildcards_obj), depth);
      }

      static Term* rewrite(Term *self, const SourceLocation *location, const void *substitutions_vtable, void *substitutions_obj) {
        return Derived::rewrite_impl(*tree_cast<Derived>(self), *location, Map<TreePtr<Term>, TreePtr<Term> >(substitutions_vtable, substitutions_obj)).release();
      }

      static void iterate(void *result, Term *self) {
        new (result) typename Derived::IteratorType(TreePtr<Derived>(tree_cast<Derived>(self)));
      }

      struct IteratorImpl {
        typedef typename Derived::IteratorType ObjectType;

        static void move_impl(ObjectType& dest, ObjectType& src) {dest.move_from(src);}
        static void destroy_impl(ObjectType& self) {self.~ObjectType();}
        static TreePtr<Term>& current_impl(ObjectType& self) {return self.current();}
        static bool next_impl(ObjectType& self) {return self.next();}
      };
    };

#define PSI_COMPILER_TERM(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &TermWrapper<derived>::match, \
    &TermWrapper<derived>::rewrite, \
    &TermWrapper<derived>::iterate, \
    PSI_ITERATOR(TermWrapper<derived>::IteratorImpl) \
  }

    class Type : public Term {
    public:
      static const SIVtable vtable;
      Type(CompileContext&, const SourceLocation&);
    };

#define PSI_COMPILER_TYPE(derived,name,super) PSI_COMPILER_TERM(derived,name,super)

    class Global;
    class Interface;

    class CompileContext {
      friend class Tree;
      struct TreeDisposer;

      std::ostream *m_error_stream;
      bool m_error_occurred;

      template<typename T>
      static std::string to_str(const T& t) {
        std::ostringstream ss;
        ss << t;
        return ss.str();
      }

      boost::intrusive::list<Tree, boost::intrusive::constant_time_size<false> > m_gc_list;

      SourceLocation m_root_location;

      TreePtr<Interface> m_macro_interface;
      TreePtr<Interface> m_argument_passing_interface;
      TreePtr<Type> m_empty_type;
      TreePtr<Term> m_metatype;

    public:
      CompileContext(std::ostream *error_stream);
      ~CompileContext();

      enum ErrorFlags {
        error_warning=1,
        error_internal=2
      };

      /// \brief Returns true if an error has occurred during compilation.
      bool error_occurred() const {return m_error_occurred;}
      
      void error(const SourceLocation&, const std::string&, unsigned=0);
      void error_throw(const SourceLocation&, const std::string&, unsigned=0) PSI_ATTRIBUTE((PSI_NORETURN));

      template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, to_str(message), flags);}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, to_str(message), flags);}

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

    template<typename A, typename B>
    void CompletionState::complete(CompileContext& compile_context, const SourceLocation& location, bool dependency, const A& body, const B& cleanup) {
      switch (m_state) {
      case completion_constructed: complete_main(body, cleanup); break;
      case completion_running:
        if (!dependency)
          compile_context.error_throw(location, "Circular dependency during code evaluation");
        break;

      case completion_finished: break;
      case completion_failed: throw CompileException();
      default: PSI_FAIL("unknown future state");
      }
    }

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
      Term* (*evaluate) (Macro*, Term*, const void*, void*, EvaluateContext*, const SourceLocation*);
      Term* (*dot) (Macro*, Term*, const SharedPtr<Parser::Expression>*,  EvaluateContext*, const SourceLocation*);
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
                             const SourceLocation& location) {
        return TreePtr<Term>(derived_vptr(this)->evaluate(this, value.get(), parameters.vptr(), parameters.object(), evaluate_context.get(), &location), false);
      }

      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const SharedPtr<Parser::Expression>& parameter,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) {
        return TreePtr<Term>(derived_vptr(this)->dot(this, value.get(), &parameter, evaluate_context.get(), &location), false);
      }
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static Term* evaluate(Macro *self,
                            Term *value,
                            const void *parameters_vtable,
                            void *parameters_object,
                            EvaluateContext *evaluate_context,
                            const SourceLocation *location) {
        return Impl::evaluate_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), TreePtr<EvaluateContext>(evaluate_context), *location).release();
      }

      static Term* dot(Macro *self,
                       Term *value,
                       const SharedPtr<Parser::Expression> *parameter,
                       EvaluateContext *evaluate_context,
                       const SourceLocation *location) {
        return Impl::dot_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), *parameter, TreePtr<EvaluateContext>(evaluate_context), *location).release();
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

      LookupResult<TreePtr<Term> > lookup(const String& name) {
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
      Term* (*evaluate) (MacroEvaluateCallback*, Term*, const void*, void*, EvaluateContext*, const SourceLocation*);
    };

    class MacroEvaluateCallback : public Tree {
    public:
      typedef MacroEvaluateCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroEvaluateCallback(CompileContext& compile_context, const SourceLocation& location)
      : Tree(compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value, const List<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) {
        return TreePtr<Term>(derived_vptr(this)->evaluate(this, value.get(), parameters.vptr(), parameters.object(), evaluate_context.get(), &location), false);
      }
    };

    template<typename Derived>
    struct MacroEvaluateCallbackWrapper : NonConstructible {
      static Term* evaluate(MacroEvaluateCallback *self, Term *value, const void *parameters_vtable, void *parameters_object, EvaluateContext *evaluate_context, const SourceLocation *location) {
        return Derived::evaluate_impl(*static_cast<Derived*>(self), TreePtr<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), TreePtr<EvaluateContext>(evaluate_context), *location).release();
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

      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) {
        return TreePtr<Term>(derived_vptr(this)->dot(this, value.get(), evaluate_context.get(), &location), false);
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
    public:
      static const TreeVtable vtable;
      Interface(CompileContext&, const SourceLocation&);

      /// \brief If the target of this interface is a compile-time type, this value gives the type of tree we're looking for.
      TreeVtable *compile_time_type;
      /// \brief If the target of this interface is a run-time value, this gives the type of that value.
      TreePtr<Term> run_time_type;

      template<typename Visitor>
      static void visit_impl(Interface& self, Visitor& visitor) {
	Tree::visit_impl(self, visitor);
	visitor
	  ("compile_time_type", self.compile_time_type)
	  ("run_time_type", self.run_time_type);
      }
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
    
    TreePtr<Term> function_definition_object(CompileContext&, const SourceLocation&);

    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroEvaluateCallback>&, const std::map<String, TreePtr<MacroDotCallback> >&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroEvaluateCallback>&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<MacroDotCallback> >&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&);
    void attach_compile_implementation(const TreePtr<Interface>&, const TreePtr<ImplementationTerm>&, const TreePtr<>&, const SourceLocation&);
    TreePtr<Term> make_macro_term(CompileContext&, const SourceLocation&);
  }
}

#endif
