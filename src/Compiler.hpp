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

    class PhysicalSourceOrigin {
    public:
      virtual std::string name() = 0;

      static boost::shared_ptr<PhysicalSourceOrigin> filename(const std::string&);
    };

    struct PhysicalSourceLocation {
      boost::shared_ptr<PhysicalSourceOrigin> origin;

      const char *begin, *end;

      int first_line;
      int first_column;
      int last_line;
      int last_column;
    };

    class LogicalSourceLocation {
    public:
      virtual ~LogicalSourceLocation();
      virtual boost::shared_ptr<LogicalSourceLocation> parent() const = 0;
      virtual std::string full_name() const = 0;
    };

    struct SourceLocation {
      PhysicalSourceLocation physical;
      boost::shared_ptr<LogicalSourceLocation> logical;

      SourceLocation(const PhysicalSourceLocation& physical_,  const boost::shared_ptr<LogicalSourceLocation>& logical_)
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
    };

    template<typename T, typename U>
    TreePtr<T> dynamic_pointer_cast(const TreePtr<U>& ptr) {
      return TreePtr<T>(dynamic_cast<T*>(ptr.get()));
    }

    class CompileContext {
      friend class CompileObject;

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

      void error(const SourceLocation&, const std::string&, unsigned=0);
      void error_throw(const SourceLocation&, const std::string&, unsigned=0) PSI_ATTRIBUTE((PSI_NORETURN));

      template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, to_str(message), flags);}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, to_str(message), flags);}

    private:
      TreePtr<Type> m_empty_type;

    public:
      TreePtr<Type> empty_type() {return m_empty_type;}
    };

    class CompileObject : public GCBase {
      CompileContext *m_compile_context;
      virtual void gc_destroy();

    public:
      CompileObject(CompileContext&);
      virtual ~CompileObject() = 0;

      /// \brief Return the compilation context this object belongs to.
      CompileContext& compile_context() const {return *m_compile_context;}
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

    class Future : public CompileObject {
    private:
      enum State {
        state_constructed,
        state_running,
        state_finished,
        state_failed
      };

      State m_state;
      SourceLocation m_location;

      virtual void run() = 0;
      void run_wrapper();

      void throw_circular_exception() PSI_ATTRIBUTE((PSI_NORETURN));
      void throw_failed_exception() PSI_ATTRIBUTE((PSI_NORETURN));

    public:
      Future(CompileContext& context, const SourceLocation& location);
      virtual ~Future();

      const SourceLocation& location() const {return m_location;}

      void call();
      void dependency_call();
    };

    class EvaluateContext;

#if 0
    class EvaluateContext {
      boost::shared_ptr<EvaluateContext> m_parent;

    public:
      EvaluateContext(boost::shared_ptr<EvaluateContext>&);
    };
#endif

    class CompileInterfaceRef {
      void *m_interface;
      TreePtr<> m_data;
      
    public:
      CompileInterfaceRef(const TreePtr<>&);

      void* interface() const {return m_interface;}
    };

    /**
     * \brief Low-level macro interface.
     */
    struct MacroInterface {
      TreePtr<> (*evaluate) (MacroInterface*,
                             const TreePtr<>&,
                             const TreePtr<>&,
                             const std::vector<boost::shared_ptr<Parser::Expression> >&,
                             CompileContext&,
                             const GCPtr<EvaluateContext>&,
                             const SourceLocation&);
      TreePtr<> (*dot) (MacroInterface*,
                        const TreePtr<>&,
                        const TreePtr<>&,
                        const boost::shared_ptr<Parser::Expression>&,
                        CompileContext&,
                        const GCPtr<EvaluateContext>&,
                        const SourceLocation&);
    };

    /**
     * \brief Wrapper to simplify implementing Macros in C++.
     */
    template<typename Callback>
    struct MacroInterfaceWrapper {
      static TreePtr<> evaluate (MacroInterface*,
                                 const TreePtr<>& macro_data,
                                 const TreePtr<>& value,
                                 const std::vector<boost::shared_ptr<Parser::Expression> >& parameters,
                                 CompileContext& compile_context,
                                 const GCPtr<EvaluateContext>& evaluate_context,
                                 const SourceLocation& location) {
        return Callback::evaluate(macro_data, value, parameters, compile_context, evaluate_context, location);
      }

      static TreePtr<> dot (MacroInterface*,
                            const TreePtr<>& macro_data,
                            const TreePtr<>& value,
                            const boost::shared_ptr<Parser::Expression>& parameter,
                            CompileContext& compile_context,
                            const GCPtr<EvaluateContext>& evaluate_context,
                            const SourceLocation& location) {
        return Callback::evaluate(macro_data, value, parameter, compile_context, evaluate_context, location);
      }
    };

    /**
     * MacroInterface factory function. Uses MacroInterfaceWrapper to create C
     * callable functions.
     */
    template<typename Callback>
    MacroInteface make_macro_interface() {
      MacroInterface mi;
      mi.evaluate = &MacroInterfaceWrapper<Callback>::evaluate;
      mi.dot = &MacroInterfaceWrapper<Callback>::dot;
      return mi;
    }

    /**
     * \brief C++ wrapper around the Macro type.
     */
    class MacroRef {
      MacroInterface *m_interface;
      TreePtr<> m_data;
      
    public:
      TreePtr<> evaluate(const TreePtr<>& value,
                         const std::vector<boost::shared_ptr<Parser::Expression> >& parameters,
                         CompileContext& compile_context,
                         const GCPtr<EvaluateContext>& evaluate_context,
                         const SourceLocation& location) {
        return m_interface->evaluate(m_interface, m_data, value, parameters, compile_context, evaluate_context, location);
      }

      TreePtr<> dot(const TreePtr<>& value,
                    const boost::shared_ptr<Parser::Expression>& parameter,
                    CompileContext& compile_context,
                    const GCPtr<EvaluateContext>& evaluate_context,
                    const SourceLocation& location) {
        return m_interface->dot(m_interface, m_data, value, parameter, compile_context, evaluate_context, location);
      }
    };

    /**
     * \brief Context used to look up names.
     */
    class EvaluateContext : public CompileObject {
    protected:
      EvaluateContext(CompileContext&);

    public:
      virtual LookupResult<TreePtr<> > lookup(const std::string& name) = 0;
    };

    class PatternMapBase {
    protected:
      struct EntryBase {
        std::vector<TreePtr<> > pattern;
      };

    public:
    };

    struct Implementation {
      std::vector<TreePtr<> > parameter_patterns;
      TreePtr<> value;
    };

    /**
     * \brief 
     */
    class Interface {
      unsigned m_n_parameters;
      std::list<Implementation> m_implementations;
      
    public:
      
    };

    class Tree;
    class Block;

    boost::shared_ptr<LogicalSourceLocation> root_location();
    boost::shared_ptr<LogicalSourceLocation> named_child_location(const boost::shared_ptr<LogicalSourceLocation>&, const std::string&);
    boost::shared_ptr<LogicalSourceLocation> anonymous_child_location(const boost::shared_ptr<LogicalSourceLocation>&);
    TreePtr<> compile_expression(const boost::shared_ptr<Parser::Expression>&, CompileContext&, const GCPtr<EvaluateContext>&, const boost::shared_ptr<LogicalSourceLocation>&, bool=true);
    TreePtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >&, CompileContext&, const GCPtr<EvaluateContext>&, const SourceLocation&);

    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::map<std::string, TreePtr<> >&);
    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::map<std::string, TreePtr<> >&, const GCPtr<EvaluateContext>&);
    
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const GCPtr<EvaluateCallback>&, const std::map<std::string, GCPtr<DotCallback> >&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const GCPtr<EvaluateCallback>&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const std::map<std::string, GCPtr<DotCallback> >&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&);

    TreePtr<> function_definition_object(CompileContext&);
  }
}

#endif
