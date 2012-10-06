#ifndef HPP_PSI_PLATFORM
#define HPP_PSI_PLATFORM

#include <stdexcept>
#include <boost/optional.hpp>

#include "Runtime.hpp"

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
    
    class PlatformLibrary {
    public:
      virtual ~PlatformLibrary();
      virtual boost::optional<void*> symbol(const std::string& name) = 0;
    };

    boost::shared_ptr<PlatformLibrary> load_shared_object(const PropertyValue& description);
    
    std::vector<std::string> split_command_line(const std::string& args);

    struct LinkerLibraryArguments {
      /**
       * \brief Directories passed to the linker via -L/foo/bar
       * 
       * Trailing slashes have been stripped off.
       */
      std::vector<std::string> dirs;
      /// \brief Libraries passed to the linker via -lfoo
      std::vector<std::string> libs;
    };

    LinkerLibraryArguments parse_linker_arguments(const std::vector<std::string>& args);
  }
}

#endif
