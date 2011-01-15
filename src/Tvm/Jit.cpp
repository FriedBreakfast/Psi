#include "Jit.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#include <dlfcn.h>

namespace Psi {
  namespace Tvm {
    JitFactory::JitFactory(const std::string& name) : m_name(name) {
    }

    JitFactory::~JitFactory() {
    }
    
    
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
      
      typedef boost::shared_ptr<Jit> (*JitFactoryCallback) (const boost::shared_ptr<JitFactory>&);
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
          throw TvmInternalError("Cannot get JIT factory method for " + name + ": " + dlerror());
        }
      }
      
      virtual ~LinuxJitFactory() {
      }
      
      virtual boost::shared_ptr<Jit> create_jit() {
        return m_callback(shared_from_this());
      }
      
      static boost::shared_ptr<JitFactory> get(const std::string& name) {
        std::string soname = "libpsi-" + name + ".so";
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
    
    boost::shared_ptr<JitFactory> JitFactory::get(const std::string& name) {
      return LinuxJitFactory::get(name);
    }
  }
}
