#include "Jit.hpp"
#include "../Platform.hpp"

#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

namespace Psi {
  namespace Tvm {
    JitFactory::JitFactory(const CompileErrorPair& error_handler, const std::string& name)
    : m_error_handler(error_handler), m_name(name) {
    }

    JitFactory::~JitFactory() {
    }

    namespace {
      bool str_nonempty(const char *s) {
        return s[0] != '\0';
      }
    }
    
    /**
     * \brief Get a JIT factory for the default JIT compiler.
     * 
     * Tries the environment variables PSI_TVM_JIT, and if that is missing reverts to
     * the built-in default.
     */
    boost::shared_ptr<JitFactory> JitFactory::get(const CompileErrorPair& error_handler) {
      PropertyValue config;
      config["tvm"]["jit"] = PSI_TVM_JIT;
      if (str_nonempty(PSI_TVM_CC_KIND)) {
        config["tvm"]["c"]["cc"] = PSI_TVM_CC_KIND;
        config["tvm"]["c"][PSI_TVM_CC_KIND]["kind"] = PSI_TVM_CC_KIND;
        if (str_nonempty(PSI_TVM_CC_PATH))
          config["tvm"]["c"][PSI_TVM_CC_KIND]["path"] = PSI_TVM_CC_PATH;
      }

#if PSI_TVM_CC_TCCLIB
      config["tvm"]["c"]["tcclib"]["kind"] = "tcclib";
      if (str_nonempty(PSI_TVM_CC_TCC_INCLUDE))
        config["tvm"]["c"]["tcclib"]["include"] = PSI_TVM_CC_TCC_INCLUDE;
      if (str_nonempty(PSI_TVM_CC_TCC_PATH))
        config["tvm"]["c"]["tcclib"]["path"] = PSI_TVM_CC_TCC_PATH;
#endif
        
#if PSI_TVM_LLVM
      config["tvm"]["llvm"]; // Ensure key is present in map
#endif

      Platform::read_configuration_files(config, "psi.cfg");

      const PropertyValue& tvm_config = config.get("tvm");
      String name = tvm_config.get("jit").str();

      return get(error_handler, name, tvm_config.get(name));
    }
    
    JitFactoryCommon::JitFactoryCommon(const CompileErrorPair& error_handler, const std::string& name, const PropertyValue& config)
    : JitFactory(error_handler, name),
    m_config(config) {
    }
    
    namespace {
      class JitAutoPtr {
      public:
        JitAutoPtr(Jit *p) : ptr(p) {}
        ~JitAutoPtr() {
          if (ptr)
            ptr->destroy();
        }
        
        Jit *release() {
          Jit *p = ptr;
          ptr = NULL;
          return p;
        }
        
      private:
        Jit *ptr;
      };
      
      struct JitWrapper {
        boost::shared_ptr<JitFactory> factory;
        Jit *jit;
        
        JitWrapper(const boost::shared_ptr<JitFactory>& factory_, JitAutoPtr& jit_)
        : factory(factory_) {
          jit = jit_.release();
        }
        
        ~JitWrapper() {
          jit->destroy();
        }
      };
    }
    
    boost::shared_ptr<Jit> JitFactoryCommon::create_jit() {
      JitAutoPtr p(m_callback(error_handler(), m_config));
      boost::shared_ptr<JitWrapper> jw = boost::make_shared<JitWrapper>(shared_from_this(), boost::ref(p));
      return boost::shared_ptr<Jit>(jw, jw->jit);
    }
  }
}
