#include "Jit.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Psi {
  namespace Tvm {
    Jit::Jit(const boost::shared_ptr<JitFactory>& factory) : m_factory(factory) {
    }

    Jit::~Jit() {
    }

    JitFactory::JitFactory(const std::string& name) : m_name(name) {
    }

    JitFactory::~JitFactory() {
    }
    
#if defined(_WIN32)

    std::string win32_error_message() {
      LPVOID error_message;
      FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&error_message, 0, NULL);
      std::string message = static_cast<const char*>(error_message);
      LocalFree(error_message);
      return message;
    }

    class WindowsJitFactory : public JitFactory, public boost::enable_shared_from_this<WindowsJitFactory> {
    public:
      class LibHandle : public boost::noncopyable {
        HMODULE m_handle;
        
      public:
        LibHandle() : m_handle(0) {}
        LibHandle(HMODULE handle) : m_handle(handle) {}
        
        ~LibHandle() {
          if (m_handle)
            FreeLibrary(m_handle);
        }
        
        HMODULE get() {return m_handle;}
        void swap(LibHandle& other) {std::swap(m_handle, other.m_handle);}
      };

    private:
      LibHandle m_handle;
      
      typedef void (*JitFactoryCallback) (const boost::shared_ptr<JitFactory>&, boost::shared_ptr<Jit>&);
      JitFactoryCallback m_callback;
      
    public:
      WindowsJitFactory(const std::string& name, LibHandle& handle)
      : JitFactory(name) {
        m_handle.swap(handle);
        
        m_callback = reinterpret_cast<JitFactoryCallback>(GetProcAddress(m_handle.get(), "tvm_jit_new"));
        if (!m_callback)
          throw TvmInternalError("Cannot get JIT factory method for " + name + ": " + win32_error_message());
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
        LibHandle handle(LoadLibrary(soname.c_str()));
        
        if (!handle.get())
          throw TvmUserError("Cannot load JIT named " + name + " (from " + soname + "): " + win32_error_message());
        
        return boost::make_shared<WindowsJitFactory>(name, boost::ref(handle));
      }
    };
#else
    class LinuxJitFactory : public JitFactory, public boost::enable_shared_from_this<LinuxJitFactory> {
    public:
      class LibHandle : public boost::noncopyable {
        void *m_handle;
        
      public:
        LibHandle() : m_handle(0) {}
        LibHandle(void *handle) : m_handle(handle) {}
        
        ~LibHandle() {
          if (m_handle)
            dlclose(m_handle);
        }
        
        void* get() {return m_handle;}
        void swap(LibHandle& other) {std::swap(m_handle, other.m_handle);}
      };

    private:
      LibHandle m_handle;
      
      typedef void (*JitFactoryCallback) (const boost::shared_ptr<JitFactory>&, boost::shared_ptr<Jit>&);
      JitFactoryCallback m_callback;
      
    public:
      LinuxJitFactory(const std::string& name, LibHandle& handle)
      : JitFactory(name) {
        m_handle.swap(handle);
        
        dlerror();
        m_callback = reinterpret_cast<JitFactoryCallback>(dlsym(m_handle.get(), "tvm_jit_new"));
        if (!m_callback) {
          const char *err = dlerror();
          std::string err_msg;
          err_msg = err ? err : "tvm_jit_new symbol is null";
          throw TvmInternalError("Cannot get JIT factory method for " + name + ": " + err_msg);
        }
      }
      
      virtual ~LinuxJitFactory() {
      }
      
      virtual boost::shared_ptr<Jit> create_jit() {
        boost::shared_ptr<Jit> result;
        m_callback(shared_from_this(), result);
        return result;
      }
      
      static boost::shared_ptr<JitFactory> get(const std::string& name) {
        std::string soname = "libpsi-tvm-" + name + ".so";
        /* 
         * I use RTLD_GLOBAL here because it's a bad idea to combine RTLD_LOCAL with
         * C++ due to vague linkage (this can break cross-library exception handling,
         * for example). See the GCC FAQ.
         */
        LibHandle handle(dlopen(soname.c_str(), RTLD_LAZY | RTLD_GLOBAL));
        
        if (!handle.get())
          throw TvmUserError("Cannot load JIT named " + name + " (from " + soname + "): " + dlerror());
        
        return boost::make_shared<LinuxJitFactory>(name, boost::ref(handle));
      }
    };
#endif

    boost::shared_ptr<JitFactory> JitFactory::get(const std::string& name) {
#if defined(_WIN32)
      return WindowsJitFactory::get(name);
#else
      return LinuxJitFactory::get(name);
#endif
    }
  }
}
