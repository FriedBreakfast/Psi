#ifndef HPP_PSI_PLATFORM_LINUX
#define HPP_PSI_PLATFORM_LINUX

#include <string>

#include "Platform.hpp"

namespace Psi {
namespace Platform {
namespace Linux {
std::string error_string(int errcode);

class LibraryLinux : public PlatformLibrary {
  std::vector<void*> m_handles;

public:
  LibraryLinux(unsigned hint=0);
  virtual ~LibraryLinux();
  virtual boost::optional<void*> symbol(const std::string& symbol);
  void add_handle(void *handle);
};
}
}
}

#endif
