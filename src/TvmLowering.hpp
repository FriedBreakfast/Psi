#ifndef HPP_PSI_TVMLOWERING
#define HPP_PSI_TVMLOWERING

#include <boost/unordered_map.hpp>

#include "Compiler.hpp"
#include "Platform.hpp"

namespace Psi {
namespace Compiler {
/**
 * \brief Base class for module implementations.
 */
class TvmModule {
  std::string m_system_module;
  std::vector<boost::shared_ptr<TvmModule> > m_dependencies;
  std::vector<boost::shared_ptr<Platform::PlatformLibrary> > m_platform_dependencies;
};

class TvmCompiler {
  struct ModuleInfo {
  };
  
  boost::unordered_map<TreePtr<Module>, boost::shared_ptr<TvmModule> > m_modules;
  std::vector<boost::shared_ptr<Platform::PlatformLibrary> > m_platform_modules;
  
public:
  void add_shared_object();
};
}
}

#endif
