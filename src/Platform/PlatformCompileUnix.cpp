#include <dlfcn.h>
#include <sstream>

#include <boost/make_shared.hpp>

#include "PlatformCompile.hpp"
#include "PlatformUnix.hpp"
#include "../Runtime.hpp"

namespace Psi {
namespace Platform {
boost::shared_ptr<PlatformLibrary> load_module(const PropertyValue& args) {
  std::vector<std::string> libs, dirs;
  if (args.has_key("libs"))
    libs = args.get("libs").str_list();
  if (args.has_key("dirs"))
    dirs = args.get("dirs").str_list();

  boost::shared_ptr<Unix::LibraryUnix> lib = boost::make_shared<Unix::LibraryUnix>(std::max<std::size_t>(1, libs.size()));
  
  /*
  * If no libraries are listed, use default-linked stuff, i.e. libc.
  */
  if (libs.empty()) {
    void *handle = dlopen(NULL, RTLD_NOW);
    if (!handle)
      throw PlatformError("Failed get handle to main executable");
    lib->add_handle(handle);
    return lib;
  }
  
  std::ostringstream ss;
  for (std::vector<std::string>::const_iterator ii = libs.begin(), ie = libs.end(); ii != ie; ++ii) {
    bool found = false;
    
    for (std::vector<std::string>::const_iterator ji = dirs.begin(), je = dirs.end(); ji != je; ++ji) {
      ss.clear();
      ss << *ji << '/' << "lib" << *ii << ".so";
      const std::string& ss_str = ss.str();
      
      if (void *handle = dlopen(ss_str.c_str(), RTLD_NOW|RTLD_GLOBAL)) {
        lib->add_handle(handle);
        found = true;
        break;
      }
    }
    
    
    if (!found) {
      // Finally, check default search path
      ss.clear();
      ss << "lib" << *ii << ".so";
      const std::string& ss_str = ss.str();
      if (void *handle = dlopen(ss_str.c_str(), RTLD_NOW|RTLD_GLOBAL)) {
        lib->add_handle(handle);
      } else {
        throw PlatformError("Shared object not found: " + *ii);
      }
    }
  }
  
  return lib;
}
}
}
