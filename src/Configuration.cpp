#include "Configuration.hpp"
#include "Config.h"
#include "Platform.hpp"

namespace Psi {
namespace {
  bool str_nonempty(const char *s) {
    return s[0] != '\0';
  }
}

/**
 * \brief Set configuration keys built into the compiler.
 */
void configuration_builtin(PropertyValue& config) {
  config["tvm"]["jit"] = PSI_TVM_JIT;
  if (str_nonempty(PSI_TVM_CC_KIND)) {
    config["tvm"]["c"]["kind"] = "c";
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
  config["tvm"]["llvm"]["kind"] = "llvm";
#endif
  
  config["jit_target"] = "host";
  config["default_target"] = "host";
  PropertyValue& host_config = config["targets"]["host"];
  host_config["tvm"] = PSI_TVM_JIT;
  host_config["cpu"] = PSI_HOST_CPU;
  host_config["cpu_version"] = PSI_HOST_CPU_VERSION;
  host_config["os"] = PSI_HOST_OS;
  host_config["abi"] = PSI_HOST_ABI;
}

/**
 * \brief Read system configuration files.
 * 
 * These are in a system dependent but fixed location.
 */
void configuration_read_files(PropertyValue& config) {
  Platform::read_configuration_files(config, "psi.cfg");
}
}
