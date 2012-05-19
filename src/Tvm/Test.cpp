#define BOOST_TEST_MAIN
#define BOOST_TEST_NO_MAIN
#include <boost/test/unit_test.hpp>

#include "Test.hpp"
#include "Assembler.hpp"

#include <cstdlib>
#include <iostream>

namespace {
  boost::shared_ptr<Psi::Tvm::JitFactory> jit_factory;
}

int main(int argc, char *argv[]) {
  const char *jit = std::getenv("PSI_TEST_JIT");
  if (!jit)
    jit = "llvm";
  
  jit_factory = Psi::Tvm::JitFactory::get(jit);
  return boost::unit_test::unit_test_main(&init_unit_test, argc, argv);
}

namespace Psi {
  namespace Tvm {
    namespace Test {
      ContextFixture::ContextFixture()
      : module(&context, "test_module"), m_jit(jit_factory->create_jit()) {
      }

      ContextFixture::~ContextFixture() {
      }

      /**
       * JIT compile some assembler code and return the value of a single symbol.
       */
      void* ContextFixture::jit_single(const char *name, const char *src) {
        AssemblerResult r = parse_and_build(module, src);
        AssemblerResult::iterator it = r.find(name);
        BOOST_REQUIRE(it != r.end());
        m_jit->add_module(&module);
        void *result = m_jit->get_symbol(it->second);
        return result;
      }
    }
  }
}
