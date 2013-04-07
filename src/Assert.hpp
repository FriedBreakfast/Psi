#ifndef HPP_PSI_ASSERT
#define HPP_PSI_ASSERT

#include <cstdlib>

#include "Config.h"
#include "CppCompiler.hpp"
#include "Export.hpp"

namespace Psi {
#if PSI_DEBUG
#define PSI_ASSERT_MSG(cond,msg) ((cond) ? void() : Psi::assert_fail(PSI_DEBUG_LOCATION(), #cond, msg))
#define PSI_ASSERT(cond) ((cond) ? void() : Psi::assert_fail(PSI_DEBUG_LOCATION(), #cond, NULL))
#define PSI_FAIL(msg) (Psi::assert_fail(PSI_DEBUG_LOCATION(), NULL, msg))
#define PSI_NOT_IMPLEMENTED() (Psi::assert_fail(PSI_DEBUG_LOCATION(), NULL, "Not implemented"))
#define PSI_WARNING(cond) (cond ? void() : Psi::warning_fail(PSI_DEBUG_LOCATION(), #cond, NULL))
#define PSI_WARNING_MSG(cond,msg) (cond ? void() : Psi::warning_fail(PSI_DEBUG_LOCATION(), #cond, msg))
#define PSI_WARNING_FAIL(msg) (Psi::warning_fail(PSI_DEBUG_LOCATION(), NULL, msg))
#define PSI_WARNING_FAIL2(msg1,msg2) (Psi::warning_fail(PSI_DEBUG_LOCATION(), msg1, msg2))
#define PSI_CHECK(cond) PSI_ASSERT(cond)
  PSI_ATTRIBUTE((PSI_NORETURN,PSI_ASSERT_EXPORT_ATTR)) void assert_fail(DebugLocation, const char *test, const char *msg);
  PSI_ASSERT_EXPORT void warning_fail(DebugLocation, const char *test, const char *msg);
#elif !PSI_DOXYGEN
#define PSI_ASSERT_MSG(cond,msg) void()
#define PSI_ASSERT(cond) void()
#define PSI_FAIL(msg) PSI_UNREACHABLE()
#define PSI_WARNING(cond) void()
#define PSI_WARNING_MSG(cond,msg) void()
#define PSI_WARNING_FAIL(msg) void()
#define PSI_WARNING_FAIL2(msg1,msg2) void()
#define PSI_NOT_IMPLEMENTED() (std::abort())
#define PSI_CHECK(cond) do {if (!(cond)) PSI_UNREACHABLE();} while(false)
#else
  /**
   * \brief Require that a condition is true.
   *
   * If \c PSI_DEBUG is nonzero, this will print an error message with
   * the file and line where the error occurred and details of the
   * error involving \c cond and \c msg, and then call
   * <c>std::abort()</c> if \c cond evaluates to false. If \c
   * PSI_DEBUG is zero, this does nothing.
   *
   * \param cond Condition required to be true.
   * \param msg Extra message to print if \c cond is false.
   */
#define PSI_ASSERT_MSG(cond,msg)
  /**
   * \brief Require that a condition is true.
   *
   * If \c PSI_DEBUG is nonzero, this will print an error message with
   * the file and line where the error occurred and details of the
   * error, and then call <c>std::abort()</c> if \c cond evaluates to
   * false. If \c PSI_DEBUG is zero, this does nothing.
   *
   * \param cond Condition required to be true.
   */
#define PSI_ASSERT(cond)
  /**
   * \brief Indicate that a failure condition has occurred.
   *
   * This should be called when a condition which should not occur
   * has. If PSI_DEBUG is nonzero, it prints the location of the error
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
   * If PSI_DEBUG is nonzero, this prints the location of the error
   * and aborts. Otherwise, it just aborts.
   */
#define PSI_NOT_IMPLEMENTED()
/**
 * \brief Check that a value evaluates to \c true.
 * 
 * The value is evaluated regardless of the debug configuration, but will raise
 * an assertion if false and debug configuration is enabled.
 */
#define PSI_CHECK(cond) 
#endif

/**
 * \param init Initialization code; always run regardless of debug configuration.
 * Variables created in this context do not leak.
 * \param cond Condition to assert on, only evaluated in debug configuration.
 */
#define PSI_ASSERT_BLOCK(init,cond) do {init; PSI_ASSERT(cond);} while(false)
}

#endif
