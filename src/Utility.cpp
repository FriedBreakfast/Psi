#include "Utility.hpp"

#include <iostream>

namespace Psi {
#ifdef PSI_DEBUG
  CheckedCastBase::~CheckedCastBase() {
  }
#endif

  void assert_fail(const char *test, const std::string& msg) {
    std::cerr << format("Assertion failed: %s: %s", test, msg) << std::endl;
    std::abort();
  }
}
