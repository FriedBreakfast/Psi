#ifndef HPP_PSI_EXPRESSIONCOMPILER
#define HPP_PSI_EXPRESSIONCOMPILER

#include "Variant.hpp"
#include "CodeGenerator.hpp"

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

      LookupResult() : m_data(conflict) {}
      LookupResult(T src) : m_data(std::move(src)) {}
      LookupResult(NoMatchType) : m_data(no_match) {}
      LookupResult(ConflictType) : m_data(conflict) {}

      LookupResult(LookupResult&& src) = default;

      bool conflict() const {return m_data.template contains<NoMatchType>();}
      bool no_match() const {return m_data.template contains<ConflictType>();}

      const T& operator * () const {return *m_data.template get<T>();}
      const T* operator -> () const {return m_data.template get<T>();}

    private:
      DataType m_data;
    };

    class EvaluateContext;
    class SourceLocation;

    class EvaluateCallback {
    public:
      virtual Value evaluate(const Value& value, const EvaluateContext& context, const SourceLocation& source) = 0;
    };

    /**
     * This type allows for member injection into unknown types. It
     * must be used in a constraint of the form <tt>Member a b</tt>,
     * where \c a is the type having the member injected and \c b is a
     * #MemberType instance.
     */
    class MemberType : public TemplateType {
    public:
      /**
       * Look up a member by name.
       */
      virtual LookupResult<std::shared_ptr<EvaluateCallback> > member_lookup(const std::string& name);

      /**
       * Evaluate a passed argument list.
       */
      virtual LookupResult<std::shared_ptr<EvaluateCallback> > evaluate(const std::vector<int>& arguments);
    };
  }
}

#endif
