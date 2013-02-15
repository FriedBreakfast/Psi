#include "Utility.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
  void print_debug_location(DebugLocation location) {
#if defined(__GNUC__) || defined(_MSC_VER)
    std::cerr << location.file << ':' << location.function << ':' << location.line;
#else
    std::cerr << location.file << ':' << location.line;
#endif
  }

  void print_fail_message(DebugLocation location, const char *test, const char *msg, const char *category_msg) {
    print_debug_location(location);
    std::cerr << ": " << category_msg << ": ";
    if (test && msg)
      std::cerr << test << ": " << msg;
    else
      std::cerr << (msg ? msg : test);
    std::cerr << std::endl;
  }
  
  void assert_fail(DebugLocation location, const char *test, const char *msg) {
    print_fail_message(location, test, msg, "assertion failed");
    std::abort();
  }

  void warning_fail(DebugLocation location, const char *test, const char *msg) {
    print_fail_message(location, test, msg, "warning");
  }
}
