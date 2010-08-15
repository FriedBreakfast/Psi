#ifndef HPP_PSI_EXPRESSIONCOMPILER
#define HPP_PSI_EXPRESSIONCOMPILER

#if 0

#include <typeinfo>

#include "Instruction.hpp"
#include "Parser.hpp"
#include "Container.hpp"
#include "Variant.hpp"

/*
 *
 * x : function (A,B,...) (a (in? out?) : D,b,c) [
 *   ...
 * ];
 */

namespace Psi {
  namespace Compiler {
    void compile_error(const std::string& msg) PSI_ATTRIBUTE((PSI_NORETURN));

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

    class UserType {
    public:
      template<typename T>
      T* cast_to() {
	return checked_pointer_static_cast<T>(cast_to_private(typeid(T)));
      }

    private:
      virtual CheckedCastBase* cast_to_private(const std::type_info&) = 0;
    };

    class EvaluateContext {
    public:
      typedef std::function<CodeValue(const SourceLocation&)> EvaluateCallback;

      /**
       * Look up a name in this context.
       */
      virtual LookupResult<EvaluateCallback> lookup(const std::string& name) const;

      virtual UserType* user_type(Type *ty) const;
    };

    /**
     * This type allows for member injection into unknown types. It
     * must be used in a constraint of the form <tt>Member a b</tt>,
     * where \c a is the type having the member injected and \c b is a
     * #MemberType instance.
     */
    class MemberType : public CheckedCastBase {
    public:
      typedef std::function<CodeValue(Value* value, const EvaluateContext&, const SourceLocation&)> EvaluateCallback;

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
    class ConstraintType : public CheckedCastBase {
    public:
      /**
       * Apply this constraint.
       *
       * \return A list of interface types associated with the given arguments.
       */
      virtual std::vector<Type> constrain(PointerList<const Parser::Expression> arguments) const = 0;
    };
  }
}

#endif

#endif
