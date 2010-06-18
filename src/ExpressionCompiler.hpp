#ifndef HPP_PSI_EXPRESSIONCOMPILER
#define HPP_PSI_EXPRESSIONCOMPILER

#include "CodeGenerator.hpp"
#include "Parser.hpp"
#include "PointerList.hpp"
#include "Variant.hpp"

/*
 *
 * x : function (A,B,...) (a (in? out?) : D,b,c) [
 *   ...
 * ];
 */

namespace Psi {
  namespace Compiler {
    struct NoMatchType {};
    const NoMatchType no_match = {};
    struct ConflictType {};
    const ConflictType conflict = {};

    template<typename T>
    class LookupResult {
    public:
      typedef Variant<NoMatchType, ConflictType, T> DataType;

      LookupResult() : m_data(ConflictType()) {}
      LookupResult(NoMatchType) : m_data(NoMatchType()) {}
      LookupResult(ConflictType) : m_data(ConflictType()) {}
      template<typename... U>
      LookupResult(U&&... args) : m_data(T(std::forward<U>(args)...)) {}

      LookupResult(LookupResult&&) = default;

      bool conflict() const {return m_data.template contains<NoMatchType>();}
      bool no_match() const {return m_data.template contains<ConflictType>();}

      const T& operator * () const {return m_data.template get<T>();}
      const T* operator -> () const {return &m_data.template get<T>();}

    private:
      DataType m_data;
    };

    class LogicalSourceLocation {
    public:
      static LogicalSourceLocation anonymous_child(const LogicalSourceLocation& parent);
      static LogicalSourceLocation root();
      static LogicalSourceLocation named_child(const LogicalSourceLocation& parent, std::string name);
    };

    typedef Parser::PhysicalSourceLocation PhysicalSourceLocation;

    struct SourceLocation {
      LogicalSourceLocation logical;
      PhysicalSourceLocation physical;
    };

    class EvaluateContext {
    public:
      typedef std::function<CodeValue(const SourceLocation&)> EvaluateCallback;

      /**
       * Look up a name in this context.
       */
      virtual LookupResult<EvaluateCallback> lookup(const std::string& name) const;
    };

    /**
     * This type allows for member injection into unknown types. It
     * must be used in a constraint of the form <tt>Member a b</tt>,
     * where \c a is the type having the member injected and \c b is a
     * #MemberType instance.
     */
    class MemberType {
    public:
      typedef std::function<CodeValue(const EvaluateContext&, const SourceLocation&)> EvaluateCallback;

      /**
       * Look up a member by name.
       */
      virtual LookupResult<EvaluateCallback> member_lookup(const std::string& name) const;

      /**
       * Evaluate a passed argument list.
       */
      virtual LookupResult<EvaluateCallback> evaluate(PointerList<const Parser::Expression> arguments) const;
    };

    struct ConstrainResult {
      /// Newly declared type variables
      std::vector<ParameterType> new_variables;
      /// Constraints which apply to this object
      std::vector<Type> constraints;
    };

    /**
     * When a function is defined, types like this allow constraint
     * definitions.
     */
    class ConstraintType {
    public:
      /**
       * Apply this constraint.
       *
       * \return A list of interface types associated with the given arguments.
       */
      virtual std::vector<Type> constrain(PointerList<const Parser::Expression> arguments) const = 0;
    };

    struct DeclareResult {
      /// Type of the object being declared
      Type type;
      /// Related constraints
      ConstrainResult constraint;
    };

    /**
     * 
     */
    class DeclareType {
    public:
      virtual std::vector<Type> declare(PointerList<const Parser::Expression> arguments) const = 0;
    };
  }
}

#endif
