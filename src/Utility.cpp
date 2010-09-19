#include "Utility.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
  void assert_fail(const char *test, const char *msg) {
    if (test && msg)
      std::cerr << "Assertion failed: " << test << ": " << msg << std::endl;
    else
      std::cerr << "Assertion failed: " << (msg ? msg : test) << std::endl;
    std::abort();
  }
}
