#include <dlfcn.h>

#include "Platform.hpp"
#include "Runtime.hpp"

namespace Psi {
  namespace Platform {
    void platform_initialize() {
    }
    
    String address_to_symbol(void *addr, void **base) {
      Dl_info info;
      if ((dladdr(addr, &info) == 0) || !info.dli_saddr)
        throw PlatformError("Cannot get symbol name from address");
      if (base)
        *base = info.dli_saddr;
      return String(info.dli_sname);
    }
  }
}
