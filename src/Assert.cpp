#include "Utility.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
  void print_fail_message(const char *file, int line, const char *test, const char *msg, const char *category_msg) {
    std::cerr << file << ':' << line << ": " << category_msg << ": ";
    if (test && msg)
      std::cerr << test << ": " << msg;
    else
      std::cerr << (msg ? msg : test);
    std::cerr << std::endl;
  }
  
  void assert_fail(const char *file, int line, const char *test, const char *msg) {
    print_fail_message(file, line, test, msg, "assertion failed");
    std::abort();
  }

  void warning_fail(const char *file, int line, const char *test, const char *msg) {
    print_fail_message(file, line, test, msg, "warning");
  }
}
