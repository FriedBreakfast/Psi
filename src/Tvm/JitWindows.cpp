#include "Jit.hpp"
#include "../Platform/PlatformWindows.hpp"

#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

namespace Psi {
  namespace Tvm {
    class WindowsJitFactory : public JitFactoryCommon {
      Platform::Windows::LibraryHandle m_handle;
      
    public:
      WindowsJitFactory(const CompileErrorPair& error_handler, const std::string& soname, const std::string& symname, Platform::Windows::LibraryHandle& handle, const PropertyValue& config)
      : JitFactoryCommon(error_handler, config) {
        swap(m_handle, handle);
        
        m_callback = reinterpret_cast<JitFactoryCallback>(GetProcAddress(m_handle.get(), symname.c_str()));
        if (!m_callback)
          error_handler.error_throw("Cannot get JIT factory method in " + soname + ": " + Platform::Windows::last_error_string());
      }
      
      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler, const PropertyValue& config) {
        boost::optional<std::string> sobase = config.path_str("kind");
        if (!sobase)
          error_handler.error_throw("JIT 'kind' key missing from configuration");
        std::string soname = "psi-tvm-" + *sobase + ".dll";
        std::string symname = "psi_tvm_jit_new_" + *sobase;
        Platform::Windows::LibraryHandle handle(LoadLibrary(soname.c_str()));
        
        if (!handle.get())
          error_handler.error_throw("Cannot load JIT named from " + soname + ": " + Platform::Windows::last_error_string());
        
        return boost::make_shared<WindowsJitFactory>(error_handler, soname, symname, boost::ref(handle), boost::cref(config));
      }
    };

    boost::shared_ptr<JitFactory> JitFactory::get_specific(const CompileErrorPair& error_handler, const PropertyValue& config) {
      return WindowsJitFactory::get(error_handler, config);
    }
  }
}
