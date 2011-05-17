#ifndef HPP_PSI_PLATFORM
#define HPP_PSI_PLATFORM

#include <stdexcept>

namespace Psi {
  namespace Platform {
    /**
     * \brief Perform any platform-specific initialization.
     */
    void platform_initialize();
    
    /**
     * \brief Convert the address of a function or global into a symbol name.
     * 
     * \param base If non-NULL, the actual base address of the symbol
     * is stored here.
     */
    String address_to_symbol(void *addr, void **base);
    
    class PlatformError : public std::runtime_error {
    public:
      PlatformError(const char*);
      PlatformError(const std::string&);
      virtual ~PlatformError() throw();
    };
  }
}

#endif
