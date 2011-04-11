#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <stdexcept>
#include <vector>

#include <boost/function.hpp>
#include <boost/optional.hpp>

#include "CppCompiler.hpp"
#include "GarbageCollection.hpp"

namespace Psi {
  namespace Parser {
    struct Expression;
  }
  
  namespace Compiler {
    class CompileException : public std::runtime_error {
    public:
      CompileException(const std::string&);
      virtual ~CompileException() throw();
    };

    enum LookupResultType {
      lookup_result_match, ///< \brief Match found
      lookup_result_none, ///< \brief No match found
      lookup_result_conflict ///< \brief Multiple ambiguous matches found
    };

    template<typename T>
    class LookupResult {
    private:
      LookupResultType m_result_type;
      boost::optional<T> m_value;

    public:
      LookupResultType type() const {return m_result_type;}
      const T& value() const {return *m_value;}
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
      void call_void();

    private:
      void dependency_call();
      void throw_circular_exception() PSI_ATTRIBUTE((PSI_NORETURN));
      void throw_failed_exception() PSI_ATTRIBUTE((PSI_NORETURN));
      void run();
      virtual std::vector<boost::shared_ptr<FutureBase> > run_callback() = 0;

      State m_state;
    };

    template<typename T>
    struct DependentValue {
      T value;
      std::vector<boost::shared_ptr<FutureBase> > dependencies;
    };

    template<typename T>
    class Future : public FutureBase {
      boost::optional<T> m_value;
      boost::function<T> m_callback;

      virtual std::vector<boost::shared_ptr<FutureBase> > run_callback() {
        DependentValue<T> result = m_callback();
        m_value = result.value;
        return result.dependencies;
      }

    public:
      T call() {
        call_void();
        return *m_value;
      }
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

    class Context {
      GCPool m_gc_pool;

    public:
      template<typename T>
      TreePtr<T> new_tree() {
        T *ptr = new T();
        m_gc_pool.add(ptr);
        return TreePtr<T>(ptr);
      }
    };

    struct PhysicalSourceLocation {
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

    class EvaluateContext {
    public:
      typedef boost::function<DependentValue<TreePtr<> >(const EvaluateContext&, const SourceLocation&)> LookupCallback;
      
      virtual LookupResult<LookupCallback> lookup(const std::string& name) = 0;
    };

    class Macro {
    public:
      virtual ~Macro();
      virtual std::string name() = 0;

      typedef boost::function<DependentValue<TreePtr<> >(const DependentValue<TreePtr<> >&,
                                                         const std::vector<boost::shared_ptr<Parser::Expression> >&,
                                                         EvaluateContext&,
                                                         const SourceLocation&)> EvaluateCallback;
      virtual LookupResult<EvaluateCallback> evaluate_lookup(const std::vector<boost::shared_ptr<Parser::Expression> >& elements) = 0;

      typedef boost::function<DependentValue<TreePtr<> >(const DependentValue<TreePtr<> >&,
                                                         const boost::shared_ptr<Parser::Expression>&,
                                                         EvaluateContext&,
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

    boost::shared_ptr<LogicalSourceLocation> root_location();
    boost::shared_ptr<LogicalSourceLocation> named_child_location(const boost::shared_ptr<LogicalSourceLocation>&, const std::string&);
    boost::shared_ptr<LogicalSourceLocation> anonymous_child_location(const boost::shared_ptr<LogicalSourceLocation>&);
    DependentValue<TreePtr<> > compile_expression(const boost::shared_ptr<Parser::Expression>&, EvaluateContext&, const boost::shared_ptr<LogicalSourceLocation>&, bool=true);
  }
}

#endif
