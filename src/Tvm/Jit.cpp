#include "Jit.hpp"
#include "../Platform.hpp"

namespace Psi {
  namespace Tvm {
    Jit::Jit(const boost::shared_ptr<JitFactory>& factory) : m_factory(factory) {
    }

    Jit::~Jit() {
    }

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
    
    boost::shared_ptr<Jit> JitFactoryCommon::create_jit() {
      boost::shared_ptr<Jit> result;
      m_callback(shared_from_this(), result, m_config);
      return result;
    }
  }
}
