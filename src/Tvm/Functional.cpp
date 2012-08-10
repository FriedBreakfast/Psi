#include "Functional.hpp"
#include "Utility.hpp"

#include <iostream>

namespace Psi {
  namespace Tvm {
    FunctionalValue::FunctionalValue(Context& context, const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location)
    : HashableValue(context, term_functional, type, hash, location) {
    }
    
    SimpleOp::SimpleOp(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(type->context(), type, hash, location) {
    }
    
    Constructor::Constructor(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(type->context(), type, hash, location) {
    }
    
    UnaryOp::UnaryOp(const ValuePtr<>& type, const ValuePtr<>& parameter, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(type->context(), type, hash, location),
    m_parameter(parameter) {
    }

    BinaryOp::BinaryOp(const ValuePtr<>& type, const ValuePtr<>& lhs, const ValuePtr<>& rhs, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(type->context(), type, hash, location),
    m_lhs(lhs),
    m_rhs(rhs) {
    }
    
    AggregateOp::AggregateOp(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(type->context(), type, hash, location) {
    }
    
    Type::Type(Context& context, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(context, ValuePtr<>(), hash, location) {
    }

    SimpleType::SimpleType(Context& context, const HashableValueSetup& hash, const SourceLocation& location)
    : Type(context, hash, location) {
    }
    
    SimpleConstructor::SimpleConstructor(const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location)
    : FunctionalValue(type->context(), type, hash, location) {
    }
  }
}
