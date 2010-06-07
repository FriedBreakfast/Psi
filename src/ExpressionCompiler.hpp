#ifndef HPP_PSI_EXPRESSIONCOMPILER
#define HPP_PSI_EXPRESSIONCOMPILER

#include "CodeGenerator.hpp"
#include "Parser.hpp"
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

      const T& operator * () const {return *m_data.template get<T>();}
      const T* operator -> () const {return m_data.template get<T>();}

    private:
      DataType m_data;
    };

    class EvaluateContext;
    class SourceLocation;

    /**
     * This type allows for member injection into unknown types. It
     * must be used in a constraint of the form <tt>Member a b</tt>,
     * where \c a is the type having the member injected and \c b is a
     * #MemberType instance.
     */
    class MemberType : public TemplateType {
    public:
      typedef std::function<Value(const Value&, const EvaluateContext&, const SourceLocation&)> EvaluateCallback;

      /**
       * This has a standard implementation, since #MemberType
       * instances should not be associated with any run-time data.
       */
      virtual InstructionList specialize(const Context& context,
                                         const std::vector<Type>& parameters,
                                         const std::shared_ptr<Value>& value);

      /**
       * Look up a member by name.
       */
      virtual LookupResult<EvaluateCallback> member_lookup(const std::string& name);

      /**
       * Evaluate a passed argument list.
       */
      virtual LookupResult<EvaluateCallback> evaluate(const std::vector<Parser::Expression>& arguments);
    };
  }
}

#endif
