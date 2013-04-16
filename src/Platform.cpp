#include "Platform.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Platform {
PlatformError::PlatformError(const char* message) : m_message(message) {
}

PlatformError::PlatformError(const std::string& message) : m_message(message) {
}

PlatformError::~PlatformError() throw() {
}

const char *PlatformError::what() const throw() {
  return m_message.c_str();
}

PlatformLibrary::~PlatformLibrary() {
}

/**
 * \brief Execute a command and check it is successful.
 * 
 * \copydoc exec_communicate
 */
void exec_communicate_check(const Path& command, const std::vector<std::string>& args, const std::string& input, std::string *output_out, std::string *output_err) {
#if PSI_DEBUG
  // In debug output capture error output whether the user requests it or not
  std::string local_output_err;
  if (!output_err)
    output_err = &local_output_err;
#endif
  
  int status = exec_communicate(command, args, input, output_out, output_err);
  if (status != 0)
    throw PlatformError(boost::str(boost::format("Child process failed (exit status %d): %s") % status % command.str()));
}

/**
 * \brief Execute a command and check it is successful.
 * 
 * \copydoc exec_communicate
 */
void exec_communicate_check(const Path& command, const std::string& input, std::string *output_out, std::string *output_err) {
  return exec_communicate_check(command, std::vector<std::string>(), input, output_out, output_err);
}
}
}
