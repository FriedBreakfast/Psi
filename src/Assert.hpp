#ifndef HPP_PSI_ASSERT
#define HPP_PSI_ASSERT

#include <cstdlib>

#include "Config.h"
#include "CppCompiler.hpp"

namespace Psi {
#ifdef PSI_DEBUG
#define PSI_ASSERT_MSG(cond,msg) ((cond) ? void() : Psi::assert_fail(PSI_DEBUG_LOCATION(), #cond, msg))
#define PSI_ASSERT(cond) ((cond) ? void() : Psi::assert_fail(PSI_DEBUG_LOCATION(), #cond, NULL))
#define PSI_FAIL(msg) (Psi::assert_fail(PSI_DEBUG_LOCATION(), NULL, msg))
#define PSI_NOT_IMPLEMENTED() (Psi::assert_fail(PSI_DEBUG_LOCATION(), NULL, "Not implemented"))
#define PSI_WARNING(cond) (cond ? void() : Psi::warning_fail(PSI_DEBUG_LOCATION(), #cond, NULL))
#define PSI_WARNING_FAIL(msg) (Psi::warning_fail(PSI_DEBUG_LOCATION(), NULL, msg))
  void assert_fail(DebugLocation, const char *test, const char *msg) PSI_ATTRIBUTE((PSI_NORETURN));
  void warning_fail(DebugLocation, const char *test, const char *msg);
#elif !defined(PSI_DOXYGEN)
#define PSI_ASSERT_MSG(cond,msg) void()
#define PSI_ASSERT(cond) void()
#define PSI_FAIL(msg) PSI_UNREACHABLE()
#define PSI_WARNING(cond) void()
#define PSI_WARNING_FAIL(msg) void()
#define PSI_NOT_IMPLEMENTED() (std::abort())
#else
#define PSI_DEBUG_UNUSED
  /**
   * \brief Require that a condition is true.
   *
   * If \c PSI_DEBUG is defined, this will print an error message with
   * the file and line where the error occurred and details of the
   * error involving \c cond and \c msg, and then call
   * <c>std::abort()</c> if \c cond evaluates to false. If \c
   * PSI_DEBUG is not defined, this does nothing.
   *
   * \param cond Condition required to be true.
   * \param msg Extra message to print if \c cond is false.
   */
#define PSI_ASSERT_MSG(cond,msg)
  /**
   * \brief Require that a condition is true.
   *
   * If \c PSI_DEBUG is defined, this will print an error message with
   * the file and line where the error occurred and details of the
   * error, and then call <c>std::abort()</c> if \c cond evaluates to
   * false. If \c PSI_DEBUG is not defined, this does nothing.
   *
   * \param cond Condition required to be true.
   */
#define PSI_ASSERT(cond)
  /**
   * \brief Indicate that a failure condition has occurred.
   *
   * This should be called when a condition which should not occur
   * has. If PSI_DEBUG is defined, it prints the location of the error
   * and \c msg and calls <c>std::abort()</c>. Otherwise, this
   * indicates to the compiler that this section of code is
   * unreachable, if the compiler supports this.
   *
   * \param msg Details of the error.
   */
#define PSI_FAIL(msg)
  /**
   * \brief Require that a condition is true, but do not abort.
   *
   * This is similar to PSI_ASSERT(), except that it does not call
   * <c>std::abort()</c>. This should be used in destructors since
   * aborting in a destructor confuses debugging.
   */
#define PSI_WARNING(cond)
  /**
   * \brief Require that a condition is true, but do not abort.
   *
   * This is similar to PSI_ASSERT(), except that it does not call
   * <c>std::abort()</c>. This should be used in destructors since
   * aborting in a destructor confuses debugging.
   */
#define PSI_WARNING_FAIL(msg)
  /**
   * \brief Indicate that unimplemented code has been reached.
   *
   * If PSI_DEBUG is defined, this prints the location of the error
   * and aborts. Otherwise, it just aborts.
   */
#define PSI_NOT_IMPLEMENTED()
#endif
}

#endif
