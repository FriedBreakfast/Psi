#include "Platform.hpp"

namespace Psi {
namespace Platform {
PlatformError::PlatformError(const char* message) : std::runtime_error(message) {
}

PlatformError::PlatformError(const std::string& message) : std::runtime_error(message) {
}

PlatformError::~PlatformError() throw() {
}
}
}
