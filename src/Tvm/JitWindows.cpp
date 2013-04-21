#include "Jit.hpp"
#include "../PlatformWindows.hpp"

#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

namespace Psi {
  namespace Tvm {
    class WindowsJitFactory : public JitFactoryCommon {
      Platform::Windows::LibraryHandle m_handle;
      
    public:
      WindowsJitFactory(const CompileErrorPair& error_handler, const std::string& name, Platform::Windows::LibraryHandle& handle, const PropertyValue& config)
      : JitFactoryCommon(error_handler, name, m_config) {
        m_handle.swap(handle);
        
        m_callback = reinterpret_cast<JitFactoryCallback>(GetProcAddress(m_handle.get(), "tvm_jit_new"));
        if (!m_callback)
          error_handler.error_throw("Cannot get JIT factory method for " + name + ": " + Platform::Windows::last_error_string());
      }
      
      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config) {
        std::string soname = "psi-tvm-" + name + ".dll";
        Platform::Windows::LibraryHandle handle(LoadLibrary(soname.c_str()));
        
        if (!handle.get())
          error_handler.error_throw("Cannot load JIT named " + name + " (from " + soname + "): " + Platform::Windows::last_error_string());
        
        return boost::make_shared<WindowsJitFactory>(error_handler, name, boost::ref(handle), boost::cref(config));
      }
    };

    boost::shared_ptr<JitFactory> JitFactory::get(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config) {
      return WindowsJitFactory::get(error_handler, name, config);
    }
  }
}
