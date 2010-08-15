#include "Utility.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
#ifdef PSI_DEBUG
  CheckedCastBase::~CheckedCastBase() {
  }
#endif

  void assert_fail(const char *test, const std::string& msg) {
    std::cerr << "Assertion failed: " << test << ": " << msg << std::endl;
    std::abort();
  }
}
