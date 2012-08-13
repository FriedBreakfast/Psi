#include "Functional.hpp"
#include "Utility.hpp"
#include "Aggregate.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    FunctionalValue::FunctionalValue(Context& context, const SourceLocation& location)
    : HashableValue(context, term_functional, location) {
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
