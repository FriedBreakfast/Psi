#ifndef HPP_PSI_ASSERT
#define HPP_PSI_ASSERT

#include "Compiler.hpp"

namespace Psi {
#ifdef PSI_DEBUG
#define PSI_ASSERT_MSG(cond,msg) ((cond) ? void() : Psi::assert_fail(#cond, msg))
#define PSI_ASSERT(cond) ((cond) ? void() : Psi::assert_fail(#cond, NULL))
#define PSI_FAIL(msg) (Psi::assert_fail(NULL, msg))
  /**
   * \brief Issue a warning. This should be used in destructors where
   * PSI_ASSERT causes confusion in debugging.
   */
#define PSI_WARNING(cond) (cond ? void() : Psi::warning_fail(#cond, NULL))

  void assert_fail(const char *test, const char *msg) PSI_ATTRIBUTE((PSI_NORETURN));
  void warning_fail(const char *test, const char *msg);
#else
#define PSI_ASSERT_MSG(cond,msg) void()
#define PSI_ASSERT(cond) void()
#define PSI_FAIL(msg) PSI_UNREACHABLE()
#define PSI_WARNING(cond) void()
#endif
}

#endif
