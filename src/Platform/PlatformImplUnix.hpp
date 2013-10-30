#ifndef HPP_PSI_PLATFORM_IMPL_LINUX
#define HPP_PSI_PLATFORM_IMPL_LINUX

#include <string>

namespace Psi {
namespace Platform {
  struct PathData {std::string path; PathData() {}; PathData(const std::string& path_) : path(path_) {}};
  struct TemporaryPathData {bool deleted;};
}
}

#endif
