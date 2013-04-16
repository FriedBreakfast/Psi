#include "Jit.hpp"
#include "../PlatformWindows.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

namespace Psi {
  namespace Tvm {
    class WindowsJitFactory : public JitFactory, public boost::enable_shared_from_this<WindowsJitFactory> {
      Platform::Windows::LibraryHandle m_handle;
      CompileErrorPair m_error_handler;
      PropertyValue m_config;
      
      typedef void (*JitFactoryCallback) (const boost::shared_ptr<JitFactory>&, boost::shared_ptr<Jit>&, const PropertyValue&);
      JitFactoryCallback m_callback;
      
    public:
      WindowsJitFactory(const CompileErrorPair& error_handler, const std::string& name, Platform::Windows::LibraryHandle& handle, const PropertyValue& config)
      : JitFactory(error_handler, name),
      m_error_handler(error_handler),
      m_config(config) {
        m_handle.swap(handle);
        
        m_callback = reinterpret_cast<JitFactoryCallback>(GetProcAddress(m_handle.get(), "tvm_jit_new"));
        if (!m_callback)
          error_handler.error_throw("Cannot get JIT factory method for " + name + ": " + Platform::Windows::last_error_string());
      }
      
      virtual ~WindowsJitFactory() {
      }
      
      virtual boost::shared_ptr<Jit> create_jit() {
        boost::shared_ptr<Jit> result;
        m_callback(shared_from_this(), result, m_config);
        return result;
      }
      
      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config) {
        std::string soname = "psi-tvm-" + name + ".dll";
        Platform::Windows::LibraryHandle handle(LoadLibrary(soname.c_str()));
        
        if (!handle.get())
          error_handler.error_throw("Cannot load JIT named " + name + " (from " + soname + "): " + Platform::Windows::last_error_string());
        
        return boost::make_shared<WindowsJitFactory>(error_handler, name, boost::ref(handle), config);
      }
    };

    boost::shared_ptr<JitFactory> JitFactory::get(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config) {
      return WindowsJitFactory::get(error_handler, name, config);
    }
  }
}
