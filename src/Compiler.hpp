#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

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
      GCPtr<Type> m_empty_type;

    public:
      GCPtr<Type> empty_type() {return m_empty_type;}
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

    class Tree : public CompileObject {
    protected:
      virtual void gc_visit(GCVisitor&);
      virtual GCPtr<Tree> rewrite_hook(const SourceLocation& location, const std::map<GCPtr<Tree>, GCPtr<Tree> >& substitutions);

    public:
      Tree(CompileContext&);
      virtual ~Tree() = 0;

      GCPtr<Tree> rewrite(const SourceLocation&, const std::map<GCPtr<Tree>, GCPtr<Tree> >&);

      GCPtr<Future> dependency;
      GCPtr<Type> type;
    };

    class EvaluateCallback : public CompileObject {
    public:
      EvaluateCallback(CompileContext&);
      virtual ~EvaluateCallback();
      
      virtual GCPtr<Tree> evaluate_callback(const GCPtr<Tree>&,
                                            const std::vector<boost::shared_ptr<Parser::Expression> >&,
                                            CompileContext&,
                                            const GCPtr<EvaluateContext>&,
                                            const SourceLocation&) = 0;
    };

    class DotCallback : public CompileObject {
    public:
      DotCallback(CompileContext&);
      virtual ~DotCallback();
      
      virtual GCPtr<Tree> dot_callback(const GCPtr<Tree>&,
                                       const boost::shared_ptr<Parser::Expression>&,
                                       CompileContext&,
                                       const GCPtr<EvaluateContext>&,
                                       const SourceLocation&) = 0;
    };

    class Macro : public CompileObject {
    public:
      Macro(CompileContext& compile_context) : CompileObject(compile_context) {}
      virtual ~Macro();
      virtual std::string name() = 0;
      virtual LookupResult<GCPtr<EvaluateCallback> > evaluate_lookup(const std::vector<boost::shared_ptr<Parser::Expression> >& elements) = 0;
      virtual LookupResult<GCPtr<DotCallback> > dot_lookup(const boost::shared_ptr<Parser::Expression>& member) = 0;
    };

    /**
     * \brief Base class for type trees.
     */
    class Type : public Tree {
    protected:
      Type(CompileContext&);
      virtual void gc_visit(GCVisitor&);
    public:
      virtual ~Type();
      GCPtr<Macro> macro;
    };

    /**
     * \brief Tree for a statement, which should be part of a block.
     */
    class Statement : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Statement(CompileContext&);
      virtual ~Statement();

      GCPtr<Statement> next;
      GCPtr<Tree> value;
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Block(CompileContext&);
      virtual ~Block();

      GCPtr<Statement> statements;
    };

    /**
     * \brief Context used to look up names.
     */
    class EvaluateContext : public CompileObject {
    protected:
      EvaluateContext(CompileContext&);

    public:
      virtual LookupResult<GCPtr<Tree> > lookup(const std::string& name) = 0;
    };

    boost::shared_ptr<LogicalSourceLocation> root_location();
    boost::shared_ptr<LogicalSourceLocation> named_child_location(const boost::shared_ptr<LogicalSourceLocation>&, const std::string&);
    boost::shared_ptr<LogicalSourceLocation> anonymous_child_location(const boost::shared_ptr<LogicalSourceLocation>&);
    GCPtr<Tree> compile_expression(const boost::shared_ptr<Parser::Expression>&, CompileContext&, const GCPtr<EvaluateContext>&, const boost::shared_ptr<LogicalSourceLocation>&, bool=true);
    GCPtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >&, CompileContext&, const GCPtr<EvaluateContext>&, const SourceLocation&);

    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::map<std::string, GCPtr<Tree> >&);
    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::map<std::string, GCPtr<Tree> >&, const GCPtr<EvaluateContext>&);
    
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const GCPtr<EvaluateCallback>&, const std::map<std::string, GCPtr<DotCallback> >&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const GCPtr<EvaluateCallback>&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&, const std::map<std::string, GCPtr<DotCallback> >&);
    GCPtr<Macro> make_interface(CompileContext&, const std::string&);

    GCPtr<Tree> function_definition_object(CompileContext&);
  }
}

#endif
