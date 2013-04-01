#include "Platform.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Platform {
PlatformError::PlatformError(const char* message) : std::runtime_error(message) {
}

PlatformError::PlatformError(const std::string& message) : std::runtime_error(message) {
}

PlatformError::~PlatformError() throw() {
}

PlatformLibrary::~PlatformLibrary() {
}

/**
 * \brief Execute a command and check it is successful.
 * 
 * \copydoc exec_communicate
 */
void exec_communicate_check(const std::vector<std::string>& command, const std::string& input, std::string *output_out, std::string *output_err) {
  int status = exec_communicate(command, input, output_out, output_err);
  if (status != 0)
    throw PlatformError(boost::str(boost::format("Child process failed (exit status %d): %s") % status % command.front()));
}
}
}
