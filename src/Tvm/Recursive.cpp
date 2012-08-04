#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Tvm {
    const char ApplyValue::operation[] = "apply";

    RecursiveParameter::RecursiveParameter(const ValuePtr<>& type, const SourceLocation& location)
    : Value(type->context(), term_recursive_parameter, type, type->source(), location) {
    }

    /**
     * \brief Create a new parameter for a recursive term.
     *
     * \param type The term's type.
     *
     * \param phantom Whether this term should be created as a phantom
     * term. This mechanism is used to inform the compiler which
     * parameters can have phantom values in them without making the
     * overall value a phantom (unless it is always a phantom).
     */
    ValuePtr<RecursiveParameter> Context::new_recursive_parameter(const ValuePtr<>& type, const SourceLocation& location) {
      return ValuePtr<RecursiveParameter>(::new RecursiveParameter(type, location));
    }

    RecursiveType::RecursiveType(const ValuePtr<>& result_type, Value *source,
                                 const std::vector<ValuePtr<RecursiveParameter> >& parameters,
                                 const SourceLocation& location)
    : Value(result_type->context(), term_recursive, result_type, source, location),
    m_result(result_type),
    m_parameters(parameters) {
    }

    /**
     * \brief Create a new recursive term.
     *
     * \param phantom Whether all applications of this term are
     * considered phantom; in this case the value assigned to this
     * term may itself be a phantom.
     */
    ValuePtr<RecursiveType> Context::new_recursive(const ValuePtr<>& result_type,
                                                   const std::vector<ValuePtr<> >& parameters,
                                                   Value *source,
                                                   const SourceLocation& location) {
      if (source_dominated(result_type->source(), source))
        goto throw_dominator;

      for (std::vector<ValuePtr<> >::const_iterator ii = parameters.begin(), ie = parameters.end(); ii != ie; ++ii) {
        if (source_dominated((*ii)->source(), source))
          goto throw_dominator;
      }

      if (false) {
      throw_dominator:
        throw TvmUserError("source specified for recursive term is not dominated by parameter and result type blocks");
      }

      std::vector<ValuePtr<RecursiveParameter> > child_parameters;
      for (std::vector<ValuePtr<> >::const_iterator ii = parameters.begin(), ie = parameters.end(); ii != ie; ++ii)
        child_parameters.push_back(new_recursive_parameter(*ii, location));
      return ValuePtr<RecursiveType>(new RecursiveType(result_type, source, child_parameters, location));
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveType::resolve(const ValuePtr<>& term) {
      return context().resolve_recursive(ValuePtr<RecursiveType>(this), term);
    }

    ValuePtr<ApplyValue> RecursiveType::apply(const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {
      return context().apply_recursive(ValuePtr<RecursiveType>(this), parameters, location);
    }

    ApplyValue::ApplyValue(Context& context,
                           const ValuePtr<RecursiveType>& recursive,
                           const std::vector<ValuePtr<> >& parameters,
                           const SourceLocation& location)
    : HashableValue(context, term_apply,
                    recursive->type(),
                    hashable_setup<ApplyValue>(recursive)(parameters),
                    location),
    m_recursive(recursive),
    m_parameters(parameters) {
    }
    
    HashableValue* ApplyValue::clone() const {
      return ::new ApplyValue(*this);
    }

    bool ApplyValue::equals(const HashableValue& other) const {
      PSI_NOT_IMPLEMENTED();
    }

    ValuePtr<ApplyValue> Context::apply_recursive(const ValuePtr<RecursiveType>& recursive,
                                                  const std::vector<ValuePtr<> >& parameters,
                                                  const SourceLocation& location) {
      return get_functional(ApplyValue(*this, recursive, parameters, location));
    }

    ValuePtr<> ApplyValue::unpack() {
      PSI_NOT_IMPLEMENTED();
    }

    /**
     * \brief Resolve an opaque term.
     */
    void Context::resolve_recursive(const ValuePtr<RecursiveType>& recursive, const ValuePtr<>& to) {
      if (recursive->type() != to->type())
        throw TvmUserError("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
        throw TvmUserError("cannot resolve recursive term to parameterized term");

      if (to->term_type() == term_apply)
        throw TvmUserError("cannot resolve recursive term to apply term, since this leads to an infinite loop in the code generator");

      if (recursive->result())
        throw TvmUserError("resolving a recursive term which has already been resolved");

      if (!source_dominated(to->source(), recursive->source()))
        throw TvmUserError("term used to resolve recursive term is not in scope");

      if (to->phantom() && !recursive->phantom())
        throw TvmUserError("non-phantom recursive term cannot be resolved to a phantom term");
      
      recursive->m_result = to;
    }
  }
}
