#include "Jit.hpp"

#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#if !PSI_TVM_JIT_STATIC
#error JitStatic.cpp should only be used when PSI_TVM_JIT_STATIC is set
#endif

namespace Psi {
  namespace Tvm {
    namespace {
      const JitRegisterStatic *static_jit_list = NULL;
    }
    
    JitRegisterStatic::JitRegisterStatic(const char *name_, JitFactoryCommon::JitFactoryCallback callback_)
    : name(name_), callback(callback_) {
      next = static_jit_list;
      static_jit_list = this;
    }
    
    class StaticJitFactory : public JitFactoryCommon {
    public:
      StaticJitFactory(JitFactoryCommon::JitFactoryCallback callback, const CompileErrorPair& error_handler, const PropertyValue& config)
      : JitFactoryCommon(error_handler, config) {
        m_callback = callback;
      }
      
      static boost::shared_ptr<JitFactory> get(const CompileErrorPair& error_handler, const PropertyValue& config) {
        boost::optional<std::string> sobase = config.path_str("kind");
        if (!sobase)
          error_handler.error_throw("JIT 'kind' key missing from configuration");
        
        for (const JitRegisterStatic *entry = static_jit_list; entry; entry = entry->next) {
          if (entry->name == *sobase)
            return boost::make_shared<StaticJitFactory>(entry->callback, error_handler, boost::cref(config));
        }
        
        error_handler.error_throw("Cannot find statically linked JIT named " + *sobase);
      }
    };

    boost::shared_ptr<JitFactory> JitFactory::get_specific(const CompileErrorPair& error_handler, const PropertyValue& config) {
      return StaticJitFactory::get(error_handler, config);
    }
  }
}
