#include "Functional.hpp"
#include "Utility.hpp"

#include <iostream>

namespace Psi {
  namespace Tvm {
    FunctionalValue::FunctionalValue(Context& context, const ValuePtr<>& type, const HashableValueSetup& hash, const SourceLocation& location)
    : HashableValue(context, term_functional, type, hash, location),
    m_operation(hash.operation()) {
    }
  }
}
