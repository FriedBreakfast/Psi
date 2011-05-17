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
      SharedPtr<std::string> url;

      const char *begin, *end;

      int first_line;
      int first_column;
      int last_line;
      int last_column;
    };
    
    struct LogicalSourceLocation {
      SharedPtr<LogicalSourceLocation> parent;
      std::string name;
    };
    
    std::string logical_location_name(const SharedPtr<LogicalSourceLocation>& location);

    struct SourceLocation {
      PhysicalSourceLocation physical;
      SharedPtr<LogicalSourceLocation> logical;

      SourceLocation(const PhysicalSourceLocation& physical_,  const SharedPtr<LogicalSourceLocation>& logical_)
      : physical(physical_), logical(logical_) {}
    };

    class Tree;
    class Type;

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

    private:
      TreePtr<Type> m_empty_type;

    public:
      TreePtr<Type> empty_type() {return m_empty_type;}
    };

    enum LookupResultType {
      lookup_result_match, ///< \brief Match found
      lookup_result_none, ///< \brief No match found
      lookup_result_conflict ///< \brief Multiple ambiguous matches found
    };

    template<typename T>
    class LookupResult {
    private:
      LookupResultType m_type;
      boost::optional<T> m_value;

      LookupResult(LookupResultType type) : m_type(type) {}
      LookupResult(LookupResultType type, const T& value) : m_type(type), m_value(value) {}

    public:
      LookupResultType type() const {return m_type;}
      const T& value() const {return *m_value;}

      static LookupResult<T> make_none() {return LookupResult<T>(lookup_result_none);}
      static LookupResult<T> make_match(const T& value) {return LookupResult<T>(lookup_result_match, value);}
    };
    
    template<typename T>
    struct CompileInterface {
      typedef T VtableType;
      T *vptr;
      TreePtr<> data;
    };
    
    class EvaluateContextRef;
    class MacroRef;

    /**
     * \brief Low-level macro interface.
     */
    struct MacroVtable {
      Tree* (*evaluate) (MacroVtable*, Tree*, Tree*, const ArrayList<SharedPtr<Parser::Expression> >*, CompileContext*, const EvaluateContextRef*, const SourceLocation*);
      Tree* (*dot) (MacroVtable*, Tree*, Tree*, const SharedPtr<Parser::Expression>*, CompileContext*, const EvaluateContextRef*, const SourceLocation*);
    };

    struct EvaluateContextVtable {
      Tree* (*lookup) (EvaluateContextVtable*, Tree*, String*);
    };

    /**
     * \brief C++ wrapper around the Macro type.
     */
    class MacroRef : public CompileInterface<MacroVtable> {
    public:
      TreePtr<> evaluate(const TreePtr<>& value,
                         const ArrayList<SharedPtr<Parser::Expression> >& parameters,
                         CompileContext& compile_context,
                         const EvaluateContextRef& evaluate_context,
                         const SourceLocation& location) const {
        return vptr->evaluate(vptr, data.get(), value.get(), &parameters, &compile_context, &evaluate_context, &location);
      }

      TreePtr<> dot(const TreePtr<>& value,
                    const SharedPtr<Parser::Expression>& parameter,
                    CompileContext& compile_context,
                    const EvaluateContextRef& evaluate_context,
                    const SourceLocation& location) const {
        return vptr->dot(vptr, data.get(), value.get(), &parameter, &compile_context, &evaluate_context, &location);
      }
    };
    
    class EvaluateContextRef : public CompileInterface<EvaluateContextVtable> {
    public:
      TreePtr<> lookup(const std::string& name) const {
        String name_s(name.c_str(), name.length());
        return TreePtr<>(vptr->lookup(vptr, data.get(), &name_s));
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
    template<typename Callback>
    class EvaluateContextWrapper : public EvaluateContextVtable {
    private:
      static Tree* lookup_impl(EvaluateContextVtable *self, Tree *macro_data, String *name) {
        return static_cast<EvaluateContextWrapper*>(self)->m_callback.lookup(TreePtr<>(macro_data), *name).release();
      }
      
      void init() {
        this->lookup = &EvaluateContextWrapper::lookup_impl;
      }
      
      Callback m_callback;
      
    public:
      EvaluateContextWrapper() {init();}
      EvaluateContextWrapper(const Callback& callback) : m_callback(callback) {init();}
    };

    class Tree;
    class Interface;
    class CompileImplementation;
    class Block;
    class GlobalTree;

    TreePtr<> compile_expression(const boost::shared_ptr<Parser::Expression>&, CompileContext&, const EvaluateContextRef&, const boost::shared_ptr<LogicalSourceLocation>&);
    TreePtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >&, CompileContext&, const EvaluateContextRef&, const SourceLocation&);

#if 0
    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::map<std::string, TreePtr<> >&);
    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::map<std::string, TreePtr<> >&, const GCPtr<EvaluateContext>&);

    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const GCPtr<EvaluateCallback>&, const std::map<std::string, GCPtr<DotCallback> >&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const GCPtr<EvaluateCallback>&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const std::map<std::string, GCPtr<DotCallback> >&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&);
#endif

    TreePtr<> interface_lookup(const TreePtr<Interface>&, const std::vector<TreePtr<> >&, CompileContext&, const SourceLocation&);
    TreePtr<CompileImplementation> compile_interface_lookup(const TreePtr<Interface>&, const std::vector<TreePtr<> >&, CompileContext&, const SourceLocation&);
    TreePtr<> function_definition_object(CompileContext&);
    
    TreePtr<GlobalTree> tree_from_address(CompileContext&, const SourceLocation&, const TreePtr<Type>&, void*);
  }
}

#endif
