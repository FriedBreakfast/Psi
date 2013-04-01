#include "Runtime.hpp"
#include "Platform.hpp"

#include <sstream>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Psi {
namespace Platform {
namespace Windows {
class ModuleWindows : public PlatformModule {
  std::vector<HMODULE> m_handles;

public:
  virtual ~ModuleWindows();
  virtual boost::optional<void*> symbol(const std::string& symbol);
  static boost::shared_ptr<PlatformModule> load(const PropertyValue& args);
};

ModuleWindows::~ModuleWindows() {
  while (!m_handles.empty()) {
    FreeLibrary(m_handles.back());
    m_handles.pop_back();
  }
}

boost::optional<void*> ModuleWindows::symbol(const std::string& symbol) {
  for (std::vector<HMODULE>::const_reverse_iterator ii = m_handles.rbegin(), ie = m_handles.rend(); ii != ie; ++ii) {
    if (void *ptr = GetProcAddress(*ii, symbol.c_str()))
      return ptr;
  }
  
  return boost::none;
}

boost::shared_ptr<PlatformModule> ModuleWindows::load(const PropertyValue& args) {
  std::vector<std::string> libs, dirs;
  if (args.has_key("libs"))
    libs = args.get("libs").str_list();
  if (args.has_key("dirs"))
    dirs = args.get("dirs").str_list();

  boost::shared_ptr<ModuleWindows> lib(new ModuleWindows);
  // Should prevent any exceptions from being thrown by std::vector::push_back
  lib->m_handles.reserve(libs.size());
  
  /*
    * If no libraries are listed, use the handle for the calling process.
    *
    * I've basically copied this behaviour from the Linux version of this file;
    * I don't know whether this will allow getting CRT functions.
    */
  if (libs.empty()) {
    // Again, to prevent exceptions in push_back so I can be lazy about
    // exception handling in dlopen().
    lib->m_handles.reserve(1);
    HMODULE handle;
    GetModuleHandleEx(0, NULL, &handle);
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
      ss << *ji << '/' << *ii << ".dll";
      const std::string& ss_str = ss.str();
      
      if (HMODULE handle = LoadLibrary(ss_str.c_str())) {
        lib->m_handles.push_back(handle);
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
        lib->m_handles.push_back(handle);
      } else {
        throw PlatformError("DLL not found: " + *ii);
      }
    }
  }
  
  return lib;
}
}

boost::shared_ptr<PlatformModule> load_library(const PropertyValue& description) {
  return Windows::ModuleWindows::load(description);
}
}
}