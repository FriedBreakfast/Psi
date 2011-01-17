#include "Utility.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
#ifdef PSI_DEBUG
  void assert_fail(const char *test, const char *msg) {
    std::cerr << "Assertion failed: ";
    if (test && msg)
      std::cerr << test << ": " << msg;
    else
      std::cerr << (msg ? msg : test);
    std::cerr << std::endl;
    std::abort();
  }

  void warning_fail(const char *test, const char *msg) {
    std::cerr << "Warning: ";
    if (test && msg)
      std::cerr << test << ": " << msg;
    else
      std::cerr << (msg ? msg : test);
    std::cerr << std::endl;
  }
#endif
}
