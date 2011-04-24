#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <sstream>
#include <stdexcept>
#include <vector>
#include <tr1/unordered_map>

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
      friend class CompileContext;
      friend class Future;

      CompileException();

    public:
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

    class CompileContext {
      friend class Future;
      friend class Tree;
      friend class EvaluateContext;

      GCPool m_gc_pool;
      std::ostream *m_error_stream, *m_warning_stream;
      bool m_error_occurred;

      template<typename T>
      static std::string to_str(const T& t) {
        std::stringstream ss;
        ss << t;
        return ss.str();
      }

    public:
      CompileContext(std::ostream *error_stream, std::ostream *warning_stream);
      ~CompileContext();

      /// \brief Returns true if an error has occurred during compilation.
      bool error_occurred() const {return m_error_occurred;}

      void error(const SourceLocation&, const std::string&);
      void error_throw(const SourceLocation&, const std::string&) PSI_ATTRIBUTE((PSI_NORETURN));
      void warning(const SourceLocation&, const std::string&);

      template<typename T> void error(const SourceLocation& loc, const T& message) {error(loc, to_str(message));}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message) {error_throw(loc, to_str(message));}
      template<typename T> void warning(const SourceLocation& loc, const T& message) {warning(loc, to_str(message));}
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

    class Future : public GCBase {
    private:
      enum State {
        state_constructed,
        state_running,
        state_finished,
        state_failed
      };

      State m_state;
      CompileContext *m_context;
      SourceLocation m_location;

      virtual void run() = 0;
      virtual void gc_destroy();
      void run_wrapper();

      void throw_circular_exception() PSI_ATTRIBUTE((PSI_NORETURN));
      void throw_failed_exception() PSI_ATTRIBUTE((PSI_NORETURN));

    public:
      Future(CompileContext& context, const SourceLocation& location);
      virtual ~Future();

      CompileContext& context() {return *m_context;}
      const SourceLocation& location() const {return m_location;}

      void call();
      void dependency_call();
    };

    class EvaluateContext;
    class Type;

    class Tree : public GCBase {
    protected:
      virtual void gc_destroy();
      virtual void gc_visit(GCVisitor&);

    public:
      Tree(CompileContext&);
      virtual ~Tree() = 0;

      GCPtr<Future> dependency;
      GCPtr<Type> type;
    };

    class Macro {
    public:
      virtual ~Macro();
      virtual std::string name() = 0;

      typedef boost::function<GCPtr<Tree>(const GCPtr<Tree>&,
                                          const std::vector<boost::shared_ptr<Parser::Expression> >&,
                                          CompileContext&,
                                          const GCPtr<EvaluateContext>&,
                                          const SourceLocation&)> EvaluateCallback;
      virtual LookupResult<EvaluateCallback> evaluate_lookup(const std::vector<boost::shared_ptr<Parser::Expression> >& elements) = 0;

      typedef boost::function<GCPtr<Tree>(const GCPtr<Tree>&,
                                          const boost::shared_ptr<Parser::Expression>&,
                                          CompileContext&,
                                          const GCPtr<EvaluateContext>&,
                                          const SourceLocation&)> DotCallback;
      virtual LookupResult<DotCallback> dot_lookup() = 0;
    };

    /**
     * \brief Base class for type trees.
     */
    class Type : public Tree {
    protected:
      Type(CompileContext&);
    public:
      virtual ~Type();
      boost::shared_ptr<Macro> macro;
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
    class EvaluateContext : public GCBase {
    protected:
      EvaluateContext(CompileContext&);
      virtual void gc_destroy();

    public:
      virtual LookupResult<GCPtr<Tree> > lookup(const std::string& name) = 0;
    };

    boost::shared_ptr<LogicalSourceLocation> root_location();
    boost::shared_ptr<LogicalSourceLocation> named_child_location(const boost::shared_ptr<LogicalSourceLocation>&, const std::string&);
    boost::shared_ptr<LogicalSourceLocation> anonymous_child_location(const boost::shared_ptr<LogicalSourceLocation>&);
    GCPtr<Tree> compile_expression(const boost::shared_ptr<Parser::Expression>&, CompileContext&, const GCPtr<EvaluateContext>&, const boost::shared_ptr<LogicalSourceLocation>&, bool=true);
    GCPtr<Block> compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >&, CompileContext&, const GCPtr<EvaluateContext>&, const SourceLocation&);

    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::tr1::unordered_map<std::string, GCPtr<Tree> >&);
    GCPtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const std::tr1::unordered_map<std::string, GCPtr<Tree> >&, const GCPtr<EvaluateContext>&);
  }
}

#endif
