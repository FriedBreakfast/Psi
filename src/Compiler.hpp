#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <sstream>
#include <stdexcept>
#include <vector>
#include <tr1/unordered_map>

#include <boost/function.hpp>
#include <boost/optional.hpp>

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
      friend class FutureBase;

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

    class Tree;

    template<typename T=Tree>
    class TreePtr : public GCPtr<T> {
    public:
      TreePtr() : GCPtr<T>() {}
      explicit TreePtr(T *ptr) : GCPtr<T>(ptr) {}

      friend void gc_visit(TreePtr<T>& ptr, GCVisitor& visitor) {
        visitor.visit_ptr(ptr);
      }
    };

    class CompileContext {
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

      template<typename T>
      TreePtr<T> new_tree() {
        T *ptr = new T();
        m_gc_pool.add(ptr);
        return TreePtr<T>(ptr);
      }

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

    class FutureBase {
    public:
      enum State {
        state_constructed,
        state_running,
        state_finished,
        state_ready,
        state_failed,
        state_failed_circular
      };

    protected:
      FutureBase(CompileContext *context, const SourceLocation& location);
      void call_void();

    private:
      void dependency_call();
      void throw_circular_exception() PSI_ATTRIBUTE((PSI_NORETURN));
      void throw_failed_exception() PSI_ATTRIBUTE((PSI_NORETURN));
      void run();
      virtual std::vector<boost::shared_ptr<FutureBase> > run_callback() = 0;

      State m_state;
      CompileContext *m_context;
      SourceLocation m_location;
    };

    template<typename T>
    struct DependentValue {
      DependentValue<T>() {}
      DependentValue<T>(const T& value_) : value(value_) {}
      DependentValue<T>(const T& value_, std::vector<boost::shared_ptr<FutureBase> > dependencies_)
      : value(value_), dependencies(dependencies_) {}
      
      T value;
      std::vector<boost::shared_ptr<FutureBase> > dependencies;
    };

    template<typename T>
    class Future : public FutureBase {
      boost::optional<T> m_value;
      boost::function<DependentValue<T>()> m_callback;

      virtual std::vector<boost::shared_ptr<FutureBase> > run_callback() {
        DependentValue<T> result = m_callback();
        m_callback.clear();
        m_value = result.value;
        return result.dependencies;
      }

      Future(CompileContext *context, const SourceLocation& location, const boost::function<DependentValue<T>()>& callback)
      : FutureBase(context, location), m_callback(callback) {
      }

    public:
      const T& call() {
        call_void();
        return *m_value;
      }

      static boost::shared_ptr<Future<T> > make(CompileContext *context, const SourceLocation& location, const boost::function<DependentValue<T>()>& callback) {
        return boost::shared_ptr<Future<T> >(new Future(context, location, callback));
      }
    };

    class EvaluateContext {
    public:
      virtual LookupResult<DependentValue<TreePtr<> > > lookup(const std::string& name) = 0;
    };

    class Macro {
    public:
      virtual ~Macro();
      virtual std::string name() = 0;

      typedef boost::function<DependentValue<TreePtr<> >(const DependentValue<TreePtr<> >&,
                                                         const std::vector<boost::shared_ptr<Parser::Expression> >&,
                                                         CompileContext&,
                                                         const boost::shared_ptr<EvaluateContext>&,
                                                         const SourceLocation&)> EvaluateCallback;
      virtual LookupResult<EvaluateCallback> evaluate_lookup(const std::vector<boost::shared_ptr<Parser::Expression> >& elements) = 0;

      typedef boost::function<DependentValue<TreePtr<> >(const DependentValue<TreePtr<> >&,
                                                         const boost::shared_ptr<Parser::Expression>&,
                                                         CompileContext&,
                                                         const boost::shared_ptr<EvaluateContext>&,
                                                         const SourceLocation&)> DotCallback;
      virtual LookupResult<DotCallback> dot_lookup() = 0;
    };

    class Type;

    class Tree : public GCBase {
    protected:
      virtual void gc_destroy();
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~Tree();

      TreePtr<Type> type;
    };

    /**
     * \brief Base class for type trees.
     */
    class Type : public Tree {
    public:
      boost::shared_ptr<Macro> macro;
    };

    /**
     * \brief Tree for a statement, which should be part of a block.
     */
    class Statement : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~Statement();

      TreePtr<Statement> next;
      TreePtr<> value;
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~Block();

      TreePtr<Statement> statements;
    };

    class EvaluateContextDictionary : public EvaluateContext {
    public:
      typedef std::tr1::unordered_map<std::string, boost::shared_ptr<Future<DependentValue<TreePtr<> > > > > NameMapType;

      NameMapType names;

      virtual LookupResult<DependentValue<TreePtr<> > > lookup(const std::string& name) {
        NameMapType::const_iterator it = names.find(name);
        if (it != names.end()) {
          return LookupResult<DependentValue<TreePtr<> > >::make_match(it->second->call());
        } else {
          return LookupResult<DependentValue<TreePtr<> > >::make_none();
        }
      }
    };

    boost::shared_ptr<LogicalSourceLocation> root_location();
    boost::shared_ptr<LogicalSourceLocation> named_child_location(const boost::shared_ptr<LogicalSourceLocation>&, const std::string&);
    boost::shared_ptr<LogicalSourceLocation> anonymous_child_location(const boost::shared_ptr<LogicalSourceLocation>&);
    DependentValue<TreePtr<> > compile_expression(const boost::shared_ptr<Parser::Expression>&, CompileContext&, const boost::shared_ptr<EvaluateContext>&, const boost::shared_ptr<LogicalSourceLocation>&, bool=true);
    DependentValue<TreePtr<Block> > compile_statement_list(const std::vector<boost::shared_ptr<Parser::NamedExpression> >& statements, CompileContext&, const boost::shared_ptr<EvaluateContext>&, const boost::shared_ptr<LogicalSourceLocation>&);
  }
}

#endif
