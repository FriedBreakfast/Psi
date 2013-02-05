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
    : Value(context, term_recursive_parameter, type, location),
    m_phantom(phantom),
    m_recursive(NULL) {
    }
    
    template<typename V>
    void RecursiveParameter::visit(V& v) {
      visit_base<Value>(v);
    }
    
    ValuePtr<RecursiveParameter> RecursiveParameter::create(const ValuePtr<>& type, bool phantom, const SourceLocation& location) {
      return ValuePtr<RecursiveParameter>(::new RecursiveParameter(type->context(), type, phantom, location));
    }
    
    Value* RecursiveParameter::disassembler_source() {
      return recursive()->disassembler_source();
    }
    
    void RecursiveParameter::check_source_hook(CheckSourceParameter&) {
      throw TvmUserError("Recursive parameter not available in this context");
    }
    
    PSI_TVM_VALUE_IMPL(RecursiveParameter, Value);
    
    namespace {
      bool recursive_source_check(Value *source, RecursiveType *rt) {
        if (!source)
          return true;
        
        RecursiveParameter *rp = dyn_cast<RecursiveParameter>(source);
        if (!rp)
          return false;
        
        return rp->recursive_ptr() == rt;
      }
    }

    RecursiveType::RecursiveType(const ValuePtr<>& result_type, ParameterList& parameters, const SourceLocation& location)
    : Value(result_type->context(), term_recursive, ValuePtr<>(), location),
    m_result_type(result_type) {
      m_parameters.swap(parameters);
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
                                                  const SourceLocation& location) {
      return ValuePtr<RecursiveType>(::new RecursiveType(result_type, parameters, location));
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveType::resolve(const ValuePtr<>& to) {
      if (m_result_type != to->type())
        throw TvmUserError("mismatch between recursive term type and resolving term type");

      if (m_result)
        throw TvmUserError("resolving a recursive term which has already been resolved");
      
      m_result = to;
    }
    
    template<typename V>
    void RecursiveType::visit(V& v) {
      visit_base<Value>(v);
      v("result", &RecursiveType::m_result)
      ("result_type", &RecursiveType::m_result_type)
      ("parameters", &RecursiveType::m_parameters);
    }
    
    Value* RecursiveType::disassembler_source() {
      return this;
    }
    
    void RecursiveType::check_source_hook(CheckSourceParameter& parameter) {
      m_result_type->check_source(parameter);
      CheckSourceParameter parameter_copy(parameter);
      for (ParameterList::iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii) {
        (*ii)->check_source(parameter_copy);
        parameter_copy.available.insert(ii->get());
      }
      m_result->check_source(parameter_copy);
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

      return RecursiveParameterResolverRewriter(recursive(), &m_parameters).rewrite(recursive()->result());
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

    /**
     * \brief Unwrap any instances of apply/recursive.
     */
    ValuePtr<> unrecurse(const ValuePtr<>& value) {
      ValuePtr<> ptr = value;
      while (ValuePtr<ApplyValue> ap = dyn_cast<ApplyValue>(ptr))
        ptr = ap->unpack();
      return ptr;
    }
  }
}
