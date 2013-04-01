#include "Jit.hpp"

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
    
    /**
     * \brief Get a JIT factory for the default JIT compiler.
     * 
     * Tries the environment variables PSI_TVM_JIT, and if that is missing reverts to
     * the built-in default.
     */
    boost::shared_ptr<JitFactory> JitFactory::get(const CompileErrorPair& error_handler) {
      const char *name = std::getenv("PSI_TVM_JIT");
      if (!name)
        name = PSI_TVM_JIT;
      return get(error_handler, name);
    }
  }
}
