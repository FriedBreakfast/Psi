#include "Jit.hpp"
#include "../UtilityWindows.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

namespace Psi {
  namespace Tvm {
    class WindowsJitFactory : public JitFactory, public boost::enable_shared_from_this<WindowsJitFactory> {
      Platform::Windows::LibraryHandle m_handle;
      
      typedef void (*JitFactoryCallback) (const boost::shared_ptr<JitFactory>&, boost::shared_ptr<Jit>&);
      JitFactoryCallback m_callback;
      
    public:
      WindowsJitFactory(const std::string& name, Platform::Windows::LibraryHandle& handle)
      : JitFactory(name) {
        m_handle.swap(handle);
        
        m_callback = reinterpret_cast<JitFactoryCallback>(GetProcAddress(m_handle.get(), "tvm_jit_new"));
        if (!m_callback)
          throw TvmInternalError("Cannot get JIT factory method for " + name + ": " + Platform::Windows::last_error_string());
      }
      
      virtual ~WindowsJitFactory() {
      }
      
      virtual boost::shared_ptr<Jit> create_jit() {
        boost::shared_ptr<Jit> result;
        m_callback(shared_from_this(), result);
        return result;
      }
      
      static boost::shared_ptr<JitFactory> get(const std::string& name) {
        std::string soname = "psi-tvm-" + name + ".dll";
        /* 
         * I use RTLD_GLOBAL here because it's a bad idea to combine RTLD_LOCAL with
         * C++ due to vague linkage (this can break cross-library exception handling,
         * for example). See the GCC FAQ.
         */
        Platform::Windows::LibraryHandle handle(LoadLibrary(soname.c_str()));
        
        if (!handle.get())
          throw TvmUserError("Cannot load JIT named " + name + " (from " + soname + "): " + Platform::Windows::last_error_string());
        
        return boost::make_shared<WindowsJitFactory>(name, boost::ref(handle));
      }
    };

    boost::shared_ptr<JitFactory> JitFactory::get(const std::string& name) {
      return WindowsJitFactory::get(name);
    }
  }
}
