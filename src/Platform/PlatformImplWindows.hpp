#ifndef HPP_PSI_PLATFORM_IMPL_WINDOWS
#define HPP_PSI_PLATFORM_IMPL_WINDOWS

#include <string>

namespace Psi {
namespace Platform {
  typedef std::wstring PathData;
  struct TemporaryPathData {bool deleted;};
}
}

#endif
