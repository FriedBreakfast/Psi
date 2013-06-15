#include "Jit.hpp"
#include "../Platform.hpp"

#include <boost/make_shared.hpp>
#include <boost/ref.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Tvm {
    JitFactory::JitFactory(const CompileErrorPair& error_handler)
    : m_error_handler(error_handler) {
    }

    JitFactory::~JitFactory() {
    }

    /**
     * \brief Get a JIT factory for the default JIT compiler.
     * 
     * \param config Global JIT configuration.
     * 
     * This function selects a single JIT configuration from the global configuration and
     * hands off to get_specific().
     */
    boost::shared_ptr<JitFactory> JitFactory::get(const CompileErrorPair& error_handler, const PropertyValue& config) {
      boost::optional<std::string> name = config.path_str("jit");
      if (!name)
        error_handler.error_throw("Default JIT not specified (configuration property 'tvm.jit' missing)");
      const PropertyValue *config_ptr = config.path_value_ptr(*name);
      if (!config_ptr)
        error_handler.error_throw(boost::format("No configuration specified for JIT type '%1%' (configuration property 'tvm.jit.%1%' missing)") % *name);
      return get_specific(error_handler, *config_ptr);
    }
    
    JitFactoryCommon::JitFactoryCommon(const CompileErrorPair& error_handler, const PropertyValue& config)
    : JitFactory(error_handler),
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
