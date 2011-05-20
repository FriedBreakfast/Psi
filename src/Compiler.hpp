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
    
    String logical_location_name(const SharedPtr<LogicalSourceLocation>& location);

    struct SourceLocation {
      PhysicalSourceLocation physical;
      SharedPtr<LogicalSourceLocation> logical;

      SourceLocation(const PhysicalSourceLocation& physical_,  const SharedPtr<LogicalSourceLocation>& logical_)
      : physical(physical_), logical(logical_) {}
    };

    class Tree;
    class Type;
    class Interface;
    class CompileImplementation;
    class Block;
    class GlobalTree;

    template<typename T=Tree>
    class TreePtr : public GCPtr<T> {
      typedef GCPtr<T> BaseType;
    public:
      TreePtr() {}
      TreePtr(T *ptr) : BaseType(ptr) {}
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
      
      const TreePtr<Interface>& macro_interface();
      const TreePtr<CompileImplementation>& statement_dependency();

    private:
      TreePtr<Type> m_empty_type;

    public:
      TreePtr<Type> empty_type() {return m_empty_type;}
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
     */
    struct MacroVtable {
      Tree* (*evaluate) (MacroVtable*, CompileImplementation*, Tree*, const ArrayList<SharedPtr<Parser::Expression> >*, CompileContext*, CompileImplementation*, const SourceLocation*);
      Tree* (*dot) (MacroVtable*, CompileImplementation*, Tree*, const SharedPtr<Parser::Expression>*, CompileContext*, CompileImplementation*, const SourceLocation*);
    };

    struct EvaluateContextVtable {
      void (*lookup) (LookupResult<TreePtr<> >*, EvaluateContextVtable*, CompileImplementation*, const String*);
    };

    /**
     * \brief C++ wrapper around the Macro type.
     */
    struct MacroRef : CompileImplementationRef<MacroVtable> {
      TreePtr<> evaluate(const TreePtr<>& value,
                         const ArrayList<SharedPtr<Parser::Expression> >& parameters,
                         CompileContext& compile_context,
                         const TreePtr<CompileImplementation>& evaluate_context,
                         const SourceLocation& location) const {
        return TreePtr<>(vptr->evaluate(vptr, data.get(), value.get(), &parameters, &compile_context, evaluate_context.get(), &location));
      }

      TreePtr<> dot(const TreePtr<>& value,
                    const SharedPtr<Parser::Expression>& parameter,
                    CompileContext& compile_context,
                    const TreePtr<CompileImplementation>& evaluate_context,
                    const SourceLocation& location) const {
        return TreePtr<>(vptr->dot(vptr, data.get(), value.get(), &parameter, &compile_context, evaluate_context.get(), &location));
      }
    };
    
    struct EvaluateContextRef : public CompileImplementationRef<EvaluateContextVtable> {
      LookupResult<TreePtr<> > lookup(const String& name) const {
        AlignedStorageFor<LookupResult<TreePtr<> > > result;
        vptr->lookup(result.ptr(), vptr, data.get(), &name);
        LookupResult<TreePtr<> > result_copy(move_ref(*result.ptr()));
        result.ptr()->~LookupResult();
        return result_copy;
      }
    };

    /**
     * \brief Wrapper to simplify implementing Macros in C++.
     */
    template<typename Callback>
    class MacroWrapper : public MacroVtable {
    private:
      static Tree* evaluate_impl (MacroVtable *self,
                                  Tree *macro_data,
                                  Tree *value,
                                  const ArrayList<SharedPtr<Parser::Expression> > *parameters,
                                  CompileContext *compile_context,
                                  const EvaluateContextRef *evaluate_context,
                                  const SourceLocation *location) {
        return static_cast<MacroWrapper*>(self)->m_callback.evaluate(TreePtr<>(macro_data), TreePtr<>(value), *parameters, *compile_context, *evaluate_context, *location).release();
      }

      static Tree* dot_impl (MacroVtable *self,
                             Tree *macro_data,
                             Tree *value,
                             const SharedPtr<Parser::Expression> *parameter,
                             CompileContext *compile_context,
                             const EvaluateContextRef *evaluate_context,
                             const SourceLocation *location) {
        return static_cast<MacroWrapper*>(self)->m_callback.dot(TreePtr<>(macro_data), TreePtr<>(value), *parameter, *compile_context, *evaluate_context, *location).release();
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
     * \brief Wrapper to simplify implementing EvaluateContext in C++.
     */
    template<typename Callback, typename TreeType>
    class EvaluateContextWrapper : public EvaluateContextVtable {
    private:
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

    class DependencyPtr {
      Dependency *m_ptr;

    public:
      DependencyPtr() : m_ptr(0) {}
      DependencyPtr(Dependency *ptr) : m_ptr(ptr) {}
      ~DependencyPtr() {if (m_ptr) m_ptr->vptr->destroy(m_ptr);}
      Dependency *release() {Dependency *p = m_ptr; m_ptr = NULL; return p;}
    };

    TreePtr<> compile_expression(const SharedPtr<Parser::Expression>&, CompileContext&, const TreePtr<CompileImplementation>&, const SharedPtr<LogicalSourceLocation>&);
    TreePtr<Block> compile_statement_list(const ArrayList<SharedPtr<Parser::NamedExpression> >&, CompileContext&, const TreePtr<CompileImplementation>&, const SourceLocation&);
    SharedPtr<LogicalSourceLocation> make_logical_location(const SharedPtr<LogicalSourceLocation>&, const String&);

    TreePtr<CompileImplementation> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<> >&, const TreePtr<CompileImplementation>&);
    TreePtr<CompileImplementation> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<> >&);

    TreePtr<> interface_lookup(const TreePtr<Interface>&, const ArrayList<TreePtr<> >&, CompileContext&, const SourceLocation&);
    TreePtr<> interface_lookup(const TreePtr<Interface>&, const TreePtr<>&, CompileContext&, const SourceLocation&);
    TreePtr<> function_definition_object(CompileContext&);
    
    TreePtr<GlobalTree> tree_from_address(CompileContext&, const SourceLocation&, const TreePtr<Type>&, void*);
  }
}

#endif
