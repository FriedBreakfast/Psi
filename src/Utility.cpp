#include "Utility.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
#ifdef PSI_DEBUG
  CheckedCastBase::~CheckedCastBase() {
  }
#endif
}
