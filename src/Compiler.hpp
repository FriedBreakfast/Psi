#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <boost/function.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

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

    struct PhysicalSourceLocation {
      SharedPtr<String> url;

      const char *begin, *end;

      int first_line;
      int first_column;
      int last_line;
      int last_column;
    };
    
    struct LogicalSourceLocation {
      SharedPtr<LogicalSourceLocation> parent;
      String name;
    };

    struct SourceLocation {
      PhysicalSourceLocation physical;
      SharedPtr<LogicalSourceLocation> logical;

      SourceLocation(const PhysicalSourceLocation& physical_,  const SharedPtr<LogicalSourceLocation>& logical_)
      : physical(physical_), logical(logical_) {}
    };

    class Tree;
    class Type;
    class CompileContext;

    template<typename T=Tree>
    class TreePtr : public GCPtr<T> {
      typedef GCPtr<T> BaseType;
    public:
      TreePtr() {}
      TreePtr(T *ptr) : BaseType(ptr) {}
      TreePtr(T *ptr, bool add_ref) : BaseType(ptr, add_ref) {}
      template<typename U> TreePtr(const TreePtr<U>& src) : BaseType(src.get()) {}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {this->reset(src.get()); return *this;}

      friend void gc_visit(TreePtr<T>& ptr, GCVisitor& visitor) {
        visitor.visit_ptr(ptr);
      }
      
      T* release() {
        T *ptr = this->get();
        intrusive_ptr_add_ref(ptr);
        this->reset();
        return ptr;
      }
    };

    template<typename T, typename U>
    TreePtr<T> dynamic_pointer_cast(const TreePtr<U>& ptr) {
      return TreePtr<T>(dynamic_cast<T*>(ptr.get()));
    }
    
    template<typename T, typename U>
    TreePtr<T> checked_pointer_cast(const TreePtr<U>& ptr) {
      PSI_ASSERT(dynamic_cast<T*>(ptr.get()) == ptr.get());
      return TreePtr<T>(static_cast<T*>(ptr.get()));
    }

    struct Dependency;

    struct DependencyVtable {
      void (*run) (Dependency*, Tree*);
      void (*gc_visit) (Dependency*, GCVisitor*);
      void (*destroy) (Dependency*);
    };

    struct Dependency {
      DependencyVtable *vptr;
    };

    /**
     * \brief Base class to simplify implementing Dependency in C++.
     */
    template<typename Derived, typename TreeType>
    class DependencyBase : public Dependency {
      static void run_impl(Dependency *self, Tree *target) {
        TreePtr<TreeType> cast_target(checked_cast<TreeType*>(target));
        static_cast<Derived*>(self)->run(cast_target);
      }

      static void gc_visit_impl(Dependency *self, GCVisitor *visitor) {
        static_cast<Derived*>(self)->gc_visit(*visitor);
      }

      static void destroy_impl(Dependency *self) {
        delete static_cast<Derived*>(self);
      }

      static DependencyVtable m_vtable;

    public:
      DependencyBase() {
        this->vptr = &m_vtable;
      }
    };

    template<typename Derived, typename TreeType>
    DependencyVtable DependencyBase<Derived, TreeType>::m_vtable = {
      &DependencyBase<Derived, TreeType>::run_impl,
      &DependencyBase<Derived, TreeType>::gc_visit_impl,
      &DependencyBase<Derived, TreeType>::destroy_impl
    };

    class DependencyPtr : public PointerBase<Dependency> {
      Dependency *m_ptr;

    public:
      DependencyPtr() {}
      DependencyPtr(Dependency *ptr) : PointerBase<Dependency>(ptr) {}
      DependencyPtr(typename MoveRef<DependencyPtr>::type ptr) {std::swap(m_ptr, move_deref(ptr).m_ptr);}
      ~DependencyPtr() {clear();}
      void clear() {if (m_ptr) {m_ptr->vptr->destroy(m_ptr); m_ptr = 0;}}
      void swap(DependencyPtr& src) {std::swap(m_ptr, src.m_ptr);}
    };

    /**
     * \brief Base class for all types used to represent code and data.
     */
    class Tree : public GCBase {
      enum CompletionState {
        completion_constructed,
        completion_running,
        completion_finished,
        completion_failed
      };

      CompileContext *m_compile_context;
      SourceLocation m_location;
      TreePtr<Type> m_type;
      CompletionState m_completion_state;
      DependencyPtr m_dependency;
      void complete_main();

    protected:
      virtual void gc_visit(GCVisitor&);
      virtual void gc_destroy();
      virtual TreePtr<> rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions);

    public:
      Tree(CompileContext&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      Tree(const TreePtr<Type>&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      Tree(CompileContext&, const SourceLocation&);
      Tree(const TreePtr<Type>&, const SourceLocation&);
      virtual ~Tree() = 0;

      /// \brief Return the compilation context this tree belongs to.
      CompileContext& compile_context() const {return *m_compile_context;}
      /// \brief Get the location associated with this tree
      const SourceLocation& location() const {return m_location;}
      /// \brief Get the type of this tree
      const TreePtr<Type>& type() const {return m_type;}
      void complete();
      void dependency_complete();
      TreePtr<> rewrite(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);
    };

    /**
     * \brief Base class for type trees.
     */
    class Type : public Tree {
    protected:
      Type(CompileContext&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      Type(const TreePtr<Type>&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      Type(CompileContext&, const SourceLocation&);
      Type(const TreePtr<Type>&, const SourceLocation&);
    public:
      virtual ~Type();
    };

    class CompileImplementation : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);
    public:
      CompileImplementation(CompileContext&, const SourceLocation&);
      CompileImplementation(CompileContext&, const SourceLocation&, DependencyPtr&);
      TreePtr<> vtable;
    };

    class GlobalTree;
    class Interface;

    class CompileContext {
      friend class Tree;

      GCPool m_gc_pool;
      std::ostream *m_error_stream;
      bool m_error_occurred;

      template<typename T>
      static std::string to_str(const T& t) {
        std::stringstream ss;
        ss << t;
        return ss.str();
      }

    public:
      CompileContext(std::ostream *error_stream);
      ~CompileContext();

      enum ErrorFlags {
        error_warning=1,
        error_internal=2
      };

      /// \brief Returns true if an error has occurred during compilation.
      bool error_occurred() const {return m_error_occurred;}
      
      void* jit_compile(const TreePtr<>&);

      void error(const SourceLocation&, const std::string&, unsigned=0);
      void error_throw(const SourceLocation&, const std::string&, unsigned=0) PSI_ATTRIBUTE((PSI_NORETURN));

      template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, to_str(message), flags);}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, to_str(message), flags);}

      TreePtr<GlobalTree> tree_from_address(const SourceLocation&, const TreePtr<Type>&, void*);
      
      const TreePtr<CompileImplementation>& statement_dependency();
      const TreePtr<Interface>& macro_interface();
      const TreePtr<Type>& empty_type();
    };
    
    template<typename T>
    class CompileImplementationRef {
      typedef void (CompileImplementationRef::*safe_bool_type) () const;
      void safe_bool_true() const {}

    public:
      typedef T VtableType;
      T *vptr;
      TreePtr<CompileImplementation> data;

      CompileImplementationRef() : vptr(NULL) {}

      operator safe_bool_type () const {return vptr ? safe_bool_true : NULL;}
      bool operator ! () const {return !vptr;}
    };

    template<typename T>
    T make_compile_interface(typename T::VtableType *vptr, const TreePtr<>& data) {
      T x;
      x.vptr = vptr;
      x.data = data;
      return x;
    }
    
    class EvaluateContextRef;
    class MacroRef;

    /**
     * \brief Low-level macro interface.
     *
     * \see MacroRef
     * \see MacroWrapper
     */
    struct MacroVtable {
      Tree* (*evaluate) (MacroVtable*, CompileImplementation*, Tree*, const ArrayList<SharedPtr<Parser::Expression> >*, CompileImplementation*, const SourceLocation*);
      Tree* (*dot) (MacroVtable*, CompileImplementation*, Tree*, const SharedPtr<Parser::Expression>*,  CompileImplementation*, const SourceLocation*);
    };

    /**
     * \brief C++ wrapper around the MacroVtable type.
     */
    struct MacroRef : CompileImplementationRef<MacroVtable> {
      TreePtr<> evaluate(const TreePtr<>& value,
                         const ArrayList<SharedPtr<Parser::Expression> >& parameters,
                         const TreePtr<CompileImplementation>& evaluate_context,
                         const SourceLocation& location) const {
        return TreePtr<>(vptr->evaluate(vptr, data.get(), value.get(), &parameters, evaluate_context.get(), &location), false);
      }

      TreePtr<> dot(const TreePtr<>& value,
                    const SharedPtr<Parser::Expression>& parameter,
                    const TreePtr<CompileImplementation>& evaluate_context,
                    const SourceLocation& location) const {
        return TreePtr<>(vptr->dot(vptr, data.get(), value.get(), &parameter, evaluate_context.get(), &location), false);
      }
    };
    
    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Callback, typename TreeType>
    class MacroWrapper : public MacroVtable {
      static Tree* evaluate_impl (MacroVtable *self,
                                  CompileImplementation *macro_data,
                                  Tree *value,
                                  const ArrayList<SharedPtr<Parser::Expression> > *parameters,
                                  CompileImplementation *evaluate_context,
                                  const SourceLocation *location) {
        return static_cast<MacroWrapper*>(self)->m_callback.evaluate
        (TreePtr<TreeType>(checked_cast<TreeType*>(macro_data)),
         TreePtr<>(value),
         *parameters,
         TreePtr<CompileImplementation>(evaluate_context),
         *location).release();
      }

      static Tree* dot_impl (MacroVtable *self,
                             CompileImplementation *macro_data,
                             Tree *value,
                             const SharedPtr<Parser::Expression> *parameter,
                             CompileImplementation *evaluate_context,
                             const SourceLocation *location) {
        return static_cast<MacroWrapper*>(self)->m_callback.dot
        (TreePtr<TreeType>(checked_cast<TreeType*>(macro_data)),
         TreePtr<>(value),
         *parameter,
         TreePtr<CompileImplementation>(evaluate_context),
         *location).release();
      }
      
      void init() {
        this->evaluate = &MacroWrapper::evaluate_impl;
        this->dot = &MacroWrapper::dot_impl;
      }
      
      Callback m_callback;
      
    public:
      MacroWrapper() {init();}
      MacroWrapper(const Callback& callback) : m_callback(callback) {init();}
    };

    /**
     * \see EvaluateContextRef
     * \see EvaluateContextWrapper
     */
    struct EvaluateContextVtable {
      void (*lookup) (LookupResult<TreePtr<> >*, EvaluateContextVtable*, CompileImplementation*, const String*);
    };

    /**
     * \brief Wrapper to simplify using EvaluateContextVtable in C++.
     */
    struct EvaluateContextRef : CompileImplementationRef<EvaluateContextVtable> {
      LookupResult<TreePtr<> > lookup(const String& name) const {
        AlignedStorageFor<LookupResult<TreePtr<> > > result;
        vptr->lookup(result.ptr(), vptr, data.get(), &name);
        LookupResult<TreePtr<> > result_copy(move_ref(*result.ptr()));
        result.ptr()->~LookupResult();
        return result_copy;
      }
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Callback, typename TreeType>
    class EvaluateContextWrapper : public EvaluateContextVtable {
      static void lookup_impl(LookupResult<TreePtr<> > *result, EvaluateContextVtable *self, CompileImplementation *data, const String *name) {
        TreePtr<TreeType> cast_data(checked_cast<TreeType*>(data));
        LookupResult<TreePtr<> > my_result = static_cast<EvaluateContextWrapper*>(self)->m_callback.lookup(cast_data, *name);
        new (result) LookupResult<TreePtr<> >(move_ref(my_result));
      }
      
      void init() {
        this->lookup = &EvaluateContextWrapper::lookup_impl;
      }
      
      Callback m_callback;
      
    public:
      EvaluateContextWrapper() {init();}
      EvaluateContextWrapper(const Callback& callback) : m_callback(callback) {init();}
    };

    /**
     * \see MacroEvaluateCallbackRef
     * \see MacroEvaluateCallbackVtable
     */
    struct MacroEvaluateCallbackVtable {
      Tree* (*evaluate) (MacroEvaluateCallbackVtable*, CompileImplementation*, Tree*, const ArrayList<SharedPtr<Parser::Expression> >*, CompileImplementation*, const SourceLocation*);
    };

    /**
     * \brief Wrapper class to ease using MacroEvaluateCallbackVtable from C++.
     */
    struct MacroEvaluateCallbackRef : CompileImplementationRef<MacroEvaluateCallbackVtable> {
      TreePtr<> evaluate(const TreePtr<>& value,
                         const ArrayList<SharedPtr<Parser::Expression> >& parameters,
                         const TreePtr<CompileImplementation>& evaluate_context,
                         const SourceLocation& location) {
        return TreePtr<>(vptr->evaluate(vptr, data.get(), value.get(), &parameters, evaluate_context.get(), &location), false);
      }
    };

    /**
     * \brief Wrapper class to ease implementing MacroEvaluateCallbackVtable in C++.
     */
    template<typename Callback, typename TreeType>
    class MacroEvaluateCallbackWrapper : public MacroEvaluateCallbackVtable {
      static Tree* evaluate_impl(MacroEvaluateCallbackVtable *self,
                                 CompileImplementation *macro_data,
                                 Tree *value,
                                 const ArrayList<SharedPtr<Parser::Expression> > *parameters,
                                 CompileImplementation *evaluate_context,
                                 const SourceLocation *location) {
        return static_cast<MacroEvaluateCallbackWrapper*>(self)->m_callback.evaluate
        (TreePtr<TreeType>(checked_cast<TreeType*>(macro_data)),
         TreePtr<>(value),
         *parameters,
         TreePtr<CompileImplementation>(evaluate_context),
         *location).release();
      }

      void init() {
        this->evaluate = &MacroEvaluateCallbackWrapper::evaluate_impl;
      }

      Callback m_callback;

    public:
      MacroEvaluateCallbackWrapper() {init();}
      MacroEvaluateCallbackWrapper(const Callback& callback) : m_callback(callback) {init();}
    };

    /**
     * \see MacroEvaluateCallbackRef
     * \see MacroEvaluateCallbackWrapper
     */
    struct MacroDotCallbackVtable {
      Tree* (*dot) (MacroDotCallbackVtable*, CompileImplementation*, Tree*, CompileImplementation*, const SourceLocation*);
    };

    /**
     * \brief Wrapper class to ease using MacroEvaluateCallbackVtable from C++.
     */
    struct MacroDotCallbackRef : CompileImplementationRef<MacroDotCallbackVtable> {
      TreePtr<> dot(const TreePtr<>& value,
                    const TreePtr<CompileImplementation>& evaluate_context,
                    const SourceLocation& location) {
        return TreePtr<>(vptr->dot(vptr, data.get(), value.get(), evaluate_context.get(), &location), false);
      }
    };

    /**
     * \brief Wrapper class to ease implementing MacroEvaluateCallbackVtable in C++.
     */
    template<typename Callback, typename TreeType>
    class MacroDotCallbackWrapper : public MacroDotCallbackVtable {
      static Tree* dot_impl(MacroEvaluateCallbackVtable *self,
                            CompileImplementation *macro_data,
                            Tree *value,
                            CompileImplementation *evaluate_context,
                            const SourceLocation *location) {
        return static_cast<MacroDotCallbackWrapper*>(self)->m_callback.dot
        (TreePtr<TreeType>(checked_cast<TreeType*>(macro_data)),
         TreePtr<>(value),
         TreePtr<CompileImplementation>(evaluate_context),
         *location).release();
      }

      void init() {
        this->dot = &MacroDotCallbackWrapper::dot_impl;
      }

      Callback m_callback;

    public:
      MacroDotCallbackWrapper() {init();}
      MacroDotCallbackWrapper(const Callback& callback) : m_callback(callback) {init();}
    };

    class Block;

    TreePtr<> compile_expression(const SharedPtr<Parser::Expression>&, const TreePtr<CompileImplementation>&, const SharedPtr<LogicalSourceLocation>&);
    TreePtr<Block> compile_statement_list(const ArrayList<SharedPtr<Parser::NamedExpression> >&, const TreePtr<CompileImplementation>&, const SourceLocation&);
    SharedPtr<LogicalSourceLocation> make_logical_location(const SharedPtr<LogicalSourceLocation>&, const String&);
    String logical_location_name(const SharedPtr<LogicalSourceLocation>& location);

    TreePtr<CompileImplementation> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<> >&, const TreePtr<CompileImplementation>&);
    TreePtr<CompileImplementation> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<> >&);

    TreePtr<> interface_lookup(const TreePtr<Interface>&, const ArrayList<TreePtr<> >&, const SourceLocation&);
    TreePtr<> interface_lookup(const TreePtr<Interface>&, const TreePtr<>&, const SourceLocation&);
    TreePtr<> function_definition_object(CompileContext&);
    
    TreePtr<GlobalTree> tree_from_address(CompileContext&, const SourceLocation&, const TreePtr<Type>&, void*);

    template<typename Wrapper>
    Wrapper compile_implementation_wrap(const TreePtr<CompileImplementation>& impl) {
      if (!impl)
        return Wrapper();
      Wrapper wrapper;
      wrapper.vptr = static_cast<typename Wrapper::VtableType*>(impl->compile_context().jit_compile(wrapper.data));
      wrapper.data = impl;
      return wrapper;
    }

    template<typename Wrapper>
    Wrapper compile_implementation_wrap(const TreePtr<>& impl, const SourceLocation& location) {
      if (!impl)
        return Wrapper();
      TreePtr<CompileImplementation> cast_impl = dynamic_pointer_cast<CompileImplementation>(impl);
      if (!cast_impl)
        impl->compile_context().error_throw(location, "Could not cast");
      return compile_implementation_wrap<Wrapper>(cast_impl);
    }

    template<typename Wrapper>
    Wrapper compile_implementation_lookup(const TreePtr<Interface>& interface, const TreePtr<>& parameter, const SourceLocation& location) {
      return compile_implementation_wrap<Wrapper>(interface_lookup(interface, parameter, location), location);
    }

    TreePtr<CompileImplementation> make_interface(CompileContext&, const SourceLocation&, const String&, const TreePtr<CompileImplementation>&, const std::map<String, TreePtr<CompileImplementation> >&);
    TreePtr<CompileImplementation> make_interface(CompileContext&, const SourceLocation&, const String&, const TreePtr<CompileImplementation>&);
    TreePtr<CompileImplementation> make_interface(CompileContext&, const SourceLocation&, const String&, const std::map<String, TreePtr<CompileImplementation> >&);
    TreePtr<CompileImplementation> make_interface(CompileContext&, const SourceLocation&, const String&);
  }
}

#endif
