#include "Functional.hpp"
#include "Utility.hpp"
#include "Aggregate.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    FunctionalValue::FunctionalValue(Context& context, const SourceLocation& location)
    : HashableValue(context, term_functional, location) {
    }
    
    bool FunctionalValue::match_impl(const FunctionalValue& PSI_UNUSED(other), std::vector<ValuePtr<> >& PSI_UNUSED(wildcards), unsigned PSI_UNUSED(depth), UprefMatchMode PSI_UNUSED(upref_mode)) const {
      return false;
    }
    
    Constructor::Constructor(Context& context, const SourceLocation& location)
    : FunctionalValue(context, location) {
    }
    
    UnaryOp::UnaryOp(const ValuePtr<>& parameter, const SourceLocation& location)
    : FunctionalValue(parameter->context(), location),
    m_parameter(parameter) {
    }

    BinaryOp::BinaryOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location)
    : FunctionalValue(lhs->context(), location),
    m_lhs(lhs),
    m_rhs(rhs) {
    }
    
    AggregateOp::AggregateOp(Context& context, const SourceLocation& location)
    : FunctionalValue(context, location) {
    }
    
    Type::Type(Context& context, const SourceLocation& location)
    : FunctionalValue(context, location) {
    }
  }
}
