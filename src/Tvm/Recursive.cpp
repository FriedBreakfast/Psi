#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Aggregate.hpp"
#include "FunctionalBuilder.hpp"
#include "Utility.hpp"

#if PSI_DEBUG
#include <iostream>
#endif

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
      return recursive() ? recursive()->disassembler_source() : NULL;
    }
    
    void RecursiveParameter::check_source_hook(CheckSourceParameter&) {
      error_context().error_throw(location(), "Recursive parameter not available in this context");
    }
    
    PSI_TVM_VALUE_IMPL(RecursiveParameter, Value);

    RecursiveType::RecursiveType(Context& context, ParameterList& parameters, const SourceLocation& location)
    : Value(context, term_recursive, ValuePtr<>(), location) {
      m_parameters.swap(parameters);
    }

    /**
     * \brief Create a new recursive term.
     *
     * \param phantom Whether all applications of this term are
     * considered phantom; in this case the value assigned to this
     * term may itself be a phantom.
     */
    ValuePtr<RecursiveType> RecursiveType::create(Context& context,
                                                  RecursiveType::ParameterList& parameters,
                                                  const SourceLocation& location) {
      ValuePtr<RecursiveType> result(::new RecursiveType(context, parameters, location));
      for (ParameterList::iterator ii = result->parameters().begin(), ie = result->parameters().end(); ii != ie; ++ii)
        (*ii)->m_recursive = result.get();
      return result;
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveType::resolve(const ValuePtr<>& to) {
      if (!isa<Metatype>(to->type()))
        error_context().error_throw(location(), "Term used to resolve recursive type is not a type");

      if (m_result)
        error_context().error_throw(location(), "resolving a recursive term which has already been resolved");
      
      m_result = to;
    }
    
    template<typename V>
    void RecursiveType::visit(V& v) {
      visit_base<Value>(v);
      v("result", &RecursiveType::m_result)
      ("parameters", &RecursiveType::m_parameters);
    }
    
    Value* RecursiveType::disassembler_source() {
      return this;
    }

#if PSI_DEBUG
    void RecursiveType::dump_parameters() {
      for (ParameterList::iterator ii = m_parameters.begin(), ie = m_parameters.end(); ii != ie; ++ii) {
        std::cerr << ii->get() << '\n';
      }
    }
#endif

    void RecursiveType::check_source_hook(CheckSourceParameter& parameter) {
      PSI_FAIL("RecursiveType check_source_hook should never be called");
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

    ApplyType::ApplyType(const ValuePtr<RecursiveType>& recursive,
                           const std::vector<ValuePtr<> >& parameters,
                           const SourceLocation& location)
    : HashableValue(recursive->context(), term_apply, location),
    m_recursive(recursive),
    m_parameters(parameters) {
    }

    ValuePtr<> ApplyType::unpack() {
      if (!recursive()->result())
        error_context().error_throw(location(), "Cannot unpack recursive term which has not been assigned");

      return RecursiveParameterResolverRewriter(recursive(), &m_parameters).rewrite(recursive()->result());
    }

    template<typename V>
    void ApplyType::visit(V& v) {
      visit_base<HashableValue>(v);
      v("recursive", &ApplyType::m_recursive)
      ("parameters", &ApplyType::m_parameters);
    }
    
    ValuePtr<> ApplyType::check_type() const {
      ValuePtr<RecursiveType> recursive = dyn_cast<RecursiveType>(m_recursive);
      if (!recursive)
        error_context().error_throw(location(), "Parameter to apply is not a recursive type");
      
      if (m_parameters.size() != recursive->parameters().size())
        error_context().error_throw(location(), "Wrong number of parameters passed to apply");
      
      RecursiveParameterResolverRewriter rewriter(recursive, &m_parameters);
      std::vector<ValuePtr<> >::const_iterator ii = m_parameters.begin(), ie = m_parameters.end();
      RecursiveType::ParameterList::const_iterator ji = recursive->parameters().begin(), je = recursive->parameters().end();
      for (; ii != ie; ++ii, ++ji) {
        PSI_ASSERT(ji != je);
        if ((*ii)->type() != rewriter.rewrite((*ji)->type()))
          error_context().error_throw(location(), "Parameter to apply has the wrong type");
      }
      PSI_ASSERT(ji == je);
      
      return FunctionalBuilder::type_type(context(), location());
    }

    void ApplyType::hashable_check_source(ApplyType& self, CheckSourceParameter& parameter) {
      if (!self.recursive()->result())
        self.error_context().error_throw(self.location(), "Apply type used before recursive type has been resolved");
      
      return self.unpack()->check_source(parameter);
    }

    PSI_TVM_HASHABLE_IMPL(ApplyType, HashableValue, apply)
  }
}
