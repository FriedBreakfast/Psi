#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Aggregate.hpp"
#include "FunctionalBuilder.hpp"
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
      if (m_result_type != to->type())
        throw TvmUserError("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
        throw TvmUserError("cannot resolve recursive term to parameterized term");

      if (m_result)
        throw TvmUserError("resolving a recursive term which has already been resolved");

      Value *to_source = to->source();
      if (RecursiveParameter *rp = dyn_cast<RecursiveParameter>(to_source))
        to_source = rp->recursive_ptr()->source();

      if (!source_dominated(to_source, source()))
        throw TvmUserError("term used to resolve recursive term is not in scope");

      if (to->phantom())
        throw TvmUserError("Recursive type cannot be resolved to a phantom term");
      
      m_result = to;
    }
    
    template<typename V>
    void RecursiveType::visit(V& v) {
      visit_base<Value>(v);
      v("result", &RecursiveType::m_result)
      ("result_type", &RecursiveType::m_result_type)
      ("parameters", &RecursiveType::m_parameters);
    }
    
    PSI_TVM_VALUE_IMPL(RecursiveType, Value)
    
    class RecursiveParameterResolverRewriter : public RewriteCallback {
      ValuePtr<RecursiveType> m_recursive;
      const std::vector<ValuePtr<> > *m_parameters;

    public:
      RecursiveParameterResolverRewriter(const ValuePtr<RecursiveType>& recursive, const std::vector<ValuePtr<> > *parameters)
      : RewriteCallback(recursive->context()), m_recursive(recursive), m_parameters(parameters) {
      }

      virtual ValuePtr<> rewrite(const ValuePtr<>& term) {
        ValuePtr<RecursiveParameter> parameter = dyn_cast<RecursiveParameter>(term);
        if (parameter && (parameter->recursive_ptr() == m_recursive.get())) {
          std::size_t index = 0;
          for (RecursiveType::ParameterList::const_iterator ii = m_recursive->parameters().begin(), ie = m_recursive->parameters().end(); ii != ie; ++ii, ++index) {
            if (parameter == *ii)
              return m_parameters->at(index);
          }
          
          PSI_FAIL("unreachable");
        }
        
        if (ValuePtr<HashableValue> hashable = dyn_cast<HashableValue>(term)) {
          return hashable->rewrite(*this);
        } else {
          return term;
        }
      }
    };

    ApplyValue::ApplyValue(const ValuePtr<>& recursive,
                           const std::vector<ValuePtr<> >& parameters,
                           const SourceLocation& location)
    : HashableValue(recursive->context(), term_apply, location),
    m_recursive(recursive),
    m_parameters(parameters) {
    }

    ValuePtr<> ApplyValue::unpack() {
      if (!recursive()->result())
        throw TvmUserError("Cannot unpack recursive term which has not been assigned");

      ValuePtr<> result = RecursiveParameterResolverRewriter(recursive(), &m_parameters).rewrite(recursive()->result());
      // Check that the source originally computed by apply operation would still be valid for the new result,
      // so that the source analysis is not broken
      PSI_ASSERT(source_dominated(result->source(), source()));
      
      return result;
    }

    template<typename V>
    void ApplyValue::visit(V& v) {
      visit_base<HashableValue>(v);
      v("recursive", &ApplyValue::m_recursive)
      ("parameters", &ApplyValue::m_parameters);
    }
    
    ValuePtr<> ApplyValue::check_type() const {
      ValuePtr<RecursiveType> recursive = dyn_cast<RecursiveType>(m_recursive);
      if (!recursive)
        throw TvmUserError("Parameter to apply is not a recursive type");
      
      if (m_parameters.size() != recursive->parameters().size())
        throw TvmUserError("Wrong number of parameters passed to apply");
      
      RecursiveParameterResolverRewriter rewriter(recursive, &m_parameters);
      std::vector<ValuePtr<> >::const_iterator ii = m_parameters.begin(), ie = m_parameters.end();
      RecursiveType::ParameterList::const_iterator ji = recursive->parameters().begin(), je = recursive->parameters().end();
      for (; ii != ie; ++ii, ++ji) {
        PSI_ASSERT(ji != je);
        if ((*ii)->type() != rewriter.rewrite((*ji)->type()))
          throw TvmUserError("Parameter to apply has the wrong type");
      }
      PSI_ASSERT(ji == je);
      
      return rewriter.rewrite(recursive->result_type());
    }

    PSI_TVM_HASHABLE_IMPL(ApplyValue, HashableValue, apply)
    
    Unrecurse::Unrecurse(const ValuePtr<>& recursive_ptr, const SourceLocation& location)
    : FunctionalValue(recursive_ptr->context(), location),
    m_recursive_ptr(recursive_ptr) {
    }

    template<typename V>
    void Unrecurse::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("recursive_ptr", &Unrecurse::m_recursive_ptr);
    }
    
    ValuePtr<> Unrecurse::check_type() const {
      ValuePtr<PointerType> ptr_type = dyn_cast<PointerType>(m_recursive_ptr->type());
      if (!ptr_type)
        throw TvmUserError("unrecurse parameter is not a pointer");
      ValuePtr<ApplyValue> apply = dyn_cast<ApplyValue>(ptr_type->target_type());
      if (!apply)
        throw TvmUserError("unrecurse parameter is not a pointer to an apply term");
      return FunctionalBuilder::pointer_type(apply->unpack(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(Unrecurse, FunctionalValue, unrecurse)
  }
}
