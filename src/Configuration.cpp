#include "Configuration.hpp"
#include "Config.h"
#include "Platform/Platform.hpp"

#include <cstdlib>
#include <fstream>

namespace Psi {
namespace {
  bool str_nonempty(const char *s) {
    return s[0] != '\0';
  }
}

/**
 * rief Set configuration keys built into the compiler.
 */
void configuration_builtin(PropertyValue& config) {
  config["tvm"]["jit"] = PSI_TVM_JIT;
  
  if (str_nonempty(PSI_TVM_CC_SYSTEM_PATH)) {
    config["tvm"]["cc"]["kind"] = "c";
    config["tvm"]["cc"]["cckind"] = PSI_TVM_CC_SYSTEM_KIND;
    config["tvm"]["cc"]["path"] = PSI_TVM_CC_SYSTEM_PATH;
  }

#if PSI_HAVE_TCC
  config["tvm"]["tcclib"]["kind"] = "c";
  config["tvm"]["tcclib"]["cckind"] = "tcclib";
  if (str_nonempty(PSI_TVM_CC_TCC_INCLUDE))
    config["tvm"]["tcclib"]["include"] = PSI_TVM_CC_TCC_INCLUDE;
  if (str_nonempty(PSI_TVM_CC_TCC_PATH))
    config["tvm"]["tcclib"]["path"] = PSI_TVM_CC_TCC_PATH;
#endif
    
#if PSI_HAVE_LLVM
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
 * Set up configuration implied by environment variables.
 */
void configuration_environment(PropertyValue& pv) {
  if (const char *env_file = std::getenv("PSI_CONFIG_FILE"))
    pv.parse_file(env_file);
  
  if (const char *env_extra = std::getenv("PSI_CONFIG_EXTRA"))
    pv.parse_configuration(env_extra);
}

/**
 * rief Read system configuration files.
 * 
 * These are in a system dependent but fixed location.
 */
void configuration_read_files(PropertyValue& config) {
  Platform::read_configuration_files(config, "psi.cfg");
}
}
