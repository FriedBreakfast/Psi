#include <dlfcn.h>
#include <sstream>

#include "Platform.hpp"
#include "Runtime.hpp"

namespace Psi {
  namespace Platform {
    class LibraryLinux : public PlatformLibrary {
      std::vector<void*> m_handles;

    public:
      virtual ~LibraryLinux();
      virtual boost::optional<void*> symbol(const std::string& symbol);
      static boost::shared_ptr<PlatformLibrary> load(const PropertyValue& args);
    };
    
    LibraryLinux::~LibraryLinux() {
      while (!m_handles.empty()) {
        dlclose(m_handles.back());
        m_handles.pop_back();
      }
    }
    
    boost::optional<void*> LibraryLinux::symbol(const std::string& symbol) {
      dlerror();
      for (std::vector<void*>::const_reverse_iterator ii = m_handles.rbegin(), ie = m_handles.rend(); ii != ie; ++ii) {
        void *ptr = dlsym(*ii, symbol.c_str());
        if (!dlerror())
          return ptr;
      }
      
      return boost::none;
    }

    boost::shared_ptr<PlatformLibrary> LibraryLinux::load(const PropertyValue& args) {
      std::vector<std::string> libs, dirs;
      if (args.has_key("libs"))
        libs = args.get("libs").str_list();
      if (args.has_key("dirs"))
        dirs = args.get("dirs").str_list();

      boost::shared_ptr<LibraryLinux> lib(new LibraryLinux);
      // Should prevent any exceptions from being thrown by std::vector::push_back
      lib->m_handles.reserve(libs.size());
      
      /*
       * If no libraries are listed, use default-linked stuff, i.e. libc.
       */
      if (libs.empty()) {
        // Again, to prevent exceptions in push_back so I can be lazy about
        // exception handling in dlopen().
        lib->m_handles.reserve(1);
        void *handle = dlopen(NULL, RTLD_LAZY);
        if (!handle)
          throw PlatformError("Failed get handle to main executable");
        lib->m_handles.push_back(handle);
        return lib;
      }
      
      std::ostringstream ss;
      for (std::vector<std::string>::const_iterator ii = libs.begin(), ie = libs.end(); ii != ie; ++ii) {
        bool found = false;
        
        for (std::vector<std::string>::const_iterator ji = dirs.begin(), je = dirs.end(); ji != je; ++ji) {
          ss.clear();
          ss << *ji << '/' << "lib" << *ii << ".so";
          const std::string& ss_str = ss.str();
          
          if (void *handle = dlopen(ss_str.c_str(), RTLD_LAZY|RTLD_GLOBAL)) {
            lib->m_handles.push_back(handle);
            found = true;
            break;
          }
        }
        
        
        if (!found) {
          // Finally, check default search path
          ss.clear();
          ss << "lib" << *ii << ".so";
          const std::string& ss_str = ss.str();
          if (void *handle = dlopen(ss_str.c_str(), RTLD_LAZY|RTLD_GLOBAL)) {
            lib->m_handles.push_back(handle);
          } else {
            throw PlatformError("Shared object not found: " + *ii);
          }
        }
      }
      
      return lib;
    }

    boost::shared_ptr<PlatformLibrary> load_library(const PropertyValue& description) {
      return LibraryLinux::load(description);
    }
  }
}
