#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "Test.hpp"
#include "Assembler.hpp"

#include <cstdlib>

namespace {
  struct JitLoader {
    boost::shared_ptr<Psi::Tvm::JitFactory> jit_factory;

    JitLoader() {
      const char *jit = std::getenv("PSI_TEST_JIT");
      if (!jit)
        jit = "llvm";
  
      jit_factory = Psi::Tvm::JitFactory::get(jit);
    }
  };

  JitLoader jit_loader;
}

namespace Psi {
  namespace Tvm {
    namespace Test {
      namespace {
        SourceLocation module_location() {
          PhysicalSourceLocation phys;
          phys.file.reset(new SourceFile());
          phys.file->url = "(test)";
          phys.first_line = phys.first_column = 1;
          phys.last_line = phys.last_column = 0;
  
          LogicalSourceLocationPtr log = LogicalSourceLocation::new_root_location();
          return SourceLocation(phys, log);
        }
      }
      
      ContextFixture::ContextFixture()
      : location(module_location()),
      module(&context, "test_module", location),
      m_jit(jit_loader.jit_factory->create_jit()) {
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
        void *result = m_jit->get_symbol(value_cast<Global>(it->second));
        return result;
      }
    }
  }
}
