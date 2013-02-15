#include "Jit.hpp"

namespace Psi {
  namespace Tvm {
    Jit::Jit(const boost::shared_ptr<JitFactory>& factory) : m_factory(factory) {
    }

    Jit::~Jit() {
    }

    JitFactory::JitFactory(const std::string& name) : m_name(name) {
    }

    JitFactory::~JitFactory() {
    }
  }
}
