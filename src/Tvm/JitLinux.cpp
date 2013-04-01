#include "Jit.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#include <dlfcn.h>

namespace Psi {
  namespace Tvm {
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
      LinuxJitFactory(const CompileErrorPair& error_handler, const std::string& name, LibHandle& handle)
      : JitFactory(error_handler, name) {
        m_handle.swap(handle);
        
        dlerror();
        m_callback = reinterpret_cast<JitFactoryCallback>(dlsym(m_handle.get(), "tvm_jit_new"));
        if (!m_callback) {
          const char *err = dlerror();
          std::string err_msg;
          err_msg = err ? err : "tvm_jit_new symbol is null";
          error_handler.error_throw("Cannot get JIT factory method for " + name + ": " + err_msg);
        }
      }
      
      virtual ~LinuxJitFactory() {
      }
      
      virtual boost::shared_ptr<Jit> create_jit() {
        boost::shared_ptr<Jit> result;
        m_callback(shared_from_this(), result);
        return result;
      }
      
      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler, const std::string& name) {
        std::string soname = "libpsi-tvm-" + name + ".so";
        /* 
         * I use RTLD_GLOBAL here because it's a bad idea to combine RTLD_LOCAL with
         * C++ due to vague linkage (this can break cross-library exception handling,
         * for example). See the GCC FAQ.
         */
        LibHandle handle(dlopen(soname.c_str(), RTLD_LAZY | RTLD_GLOBAL));
        
        if (!handle.get())
          error_handler.error_throw("Cannot load JIT named " + name + " (from " + soname + "): " + dlerror());
        
        return boost::make_shared<LinuxJitFactory>(error_handler, name, boost::ref(handle));
      }
    };

    boost::shared_ptr<JitFactory> JitFactory::get(const CompileErrorPair& error_handler, const std::string& name) {
      return LinuxJitFactory::get(error_handler, name);
    }
  }
}
