#include "../Runtime.hpp"
#include "PlatformWindows.hpp"
#include "PlatformCompile.hpp"

#include <sstream>
#include <boost/make_shared.hpp>

namespace Psi {
namespace Platform {
boost::shared_ptr<PlatformLibrary> load_module(const PropertyValue& args) {
  std::vector<std::string> libs, dirs;
  if (args.has_key("libs"))
    libs = args.get("libs").str_list();
  if (args.has_key("dirs"))
    dirs = args.get("dirs").str_list();

  boost::shared_ptr<Windows::LibraryWindows> lib = boost::make_shared<Windows::LibraryWindows>(std::max<std::size_t>(libs.size(), 1));
  
  /*
    * If no libraries are listed, use the handle for the calling process.
    *
    * I've basically copied this behaviour from the Linux version of this file;
    * I don't know whether this will allow getting CRT functions.
    */
  if (libs.empty()) {
    // Again, to prevent exceptions in push_back so I can be lazy about
    // exception handling in dlopen().
    HMODULE handle;
    GetModuleHandleEx(0, NULL, &handle);
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
      ss << *ji << '/' << *ii << ".dll";
      const std::string& ss_str = ss.str();
      
      if (HMODULE handle = LoadLibrary(ss_str.c_str())) {
        lib->add_handle(handle);
        found = true;
        break;
      }
    }
    
    
    if (!found) {
      // Finally, check default search path
      ss.clear();
      ss << *ii << ".dll";
      const std::string& ss_str = ss.str();
      if (HMODULE handle = LoadLibrary(ss_str.c_str())) {
        lib->add_handle(handle);
      } else {
        throw PlatformError("DLL not found: " + *ii);
      }
    }
  }
  
  return lib;
}
}
}
