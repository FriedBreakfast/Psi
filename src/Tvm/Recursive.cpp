#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Tvm {
    RecursiveParameter::RecursiveParameter(Context& context, const ValuePtr<>& type, bool phantom, const SourceLocation& location)
    : Value(context, term_recursive_parameter, type, this, location),
    m_phantom(phantom),
    m_recursive(NULL) {
      PSI_ASSERT(!type->parameterized());
    }
    
    template<typename V>
    void RecursiveParameter::visit(V& v) {
      visit_base<Value>(v);
    }
    
    ValuePtr<RecursiveParameter> RecursiveParameter::create(const ValuePtr<>& type, bool phantom, const SourceLocation& location) {
      return ValuePtr<RecursiveParameter>(::new RecursiveParameter(type->context(), type, phantom, location));
    }
    
    PSI_TVM_VALUE_IMPL(RecursiveParameter, Value);

    RecursiveType::RecursiveType(const ValuePtr<>& result_type, ParameterList& parameters, Value *source, const SourceLocation& location)
    : Value(result_type->context(), term_recursive, ValuePtr<>(), source, location),
    m_result_type(result_type) {
      m_parameters.swap(parameters);
      
      if (source && source->phantom())
        throw TvmUserError("Recursive types cannot be phantom");

      Value *test_source = m_parameters.front().get();
      bool phantom_finished = true;
      for (RecursiveType::ParameterList::const_iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii) {
        if ((*ii)->phantom()) {
          if (phantom_finished)
            throw TvmUserError("Phantom parameters must come before all others in a parameter list");
        } else {
          phantom_finished = true;
        }

        (*ii)->m_recursive = this;

        if (!source_dominated((*ii)->source(), test_source))
          throw TvmUserError("source specified for recursive term is not dominated by parameter block");
      }

      if (!source_dominated(result_type->source(), test_source))
        throw TvmUserError("source specified for recursive term is not dominated by result type block");
    }

    /**
     * \brief Create a new recursive term.
     *
     * \param phantom Whether all applications of this term are
     * considered phantom; in this case the value assigned to this
     * term may itself be a phantom.
     */
    ValuePtr<RecursiveType> RecursiveType::create(const ValuePtr<>& result_type,
                                                  RecursiveType::ParameterList& parameters,
                                                  Value *source,
                                                  const SourceLocation& location) {
      return ValuePtr<RecursiveType>(::new RecursiveType(result_type, parameters, source, location));
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveType::resolve(const ValuePtr<>& to) {
      if (type() != to->type())
        throw TvmUserError("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
        throw TvmUserError("cannot resolve recursive term to parameterized term");

      if (m_result)
        throw TvmUserError("resolving a recursive term which has already been resolved");

      if (!source_dominated(to->source(), source()))
        throw TvmUserError("term used to resolve recursive term is not in scope");

      if (to->phantom())
        throw TvmUserError("Recursive type cannot be resolved to a phantom term");
      
      m_result = to;
    }
    
    template<typename V>
    void RecursiveType::visit(V& v) {
      visit_base<Value>(v);
      v("result", &RecursiveType::m_result)
      ("parameters", &RecursiveType::m_parameters);
    }
    
    PSI_TVM_VALUE_IMPL(RecursiveType, Value)

    ApplyValue::ApplyValue(const ValuePtr<>& recursive,
                           const std::vector<ValuePtr<> >& parameters,
                           const SourceLocation& location)
    : HashableValue(recursive->context(), term_apply, location),
    m_recursive(recursive),
    m_parameters(parameters) {
    }

    ValuePtr<> ApplyValue::unpack() {
      ValuePtr<> result;
      PSI_NOT_IMPLEMENTED();
      // Check that the source originally computed by apply operation would still be valid for the new result,
      // so that the source analysis is not broken
      PSI_ASSERT(source_dominated(result->source(), source()));
    }

    template<typename V>
    void ApplyValue::visit(V& v) {
      visit_base<HashableValue>(v);
      v("recursive", &ApplyValue::m_recursive)
      ("parameters", &ApplyValue::m_parameters);
    }
    
    ValuePtr<> ApplyValue::check_type() const {
      if (!isa<RecursiveType>(m_recursive))
        throw TvmUserError("Parameter to apply is not a recursive type");
      return m_recursive->type();
    }

    PSI_TVM_HASHABLE_IMPL(ApplyValue, HashableValue, apply)
  }
}
