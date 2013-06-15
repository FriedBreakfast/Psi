#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>
#include <iostream>

#include "Test.hpp"
#include "Assembler.hpp"
#include "../Platform.hpp"
#include "../Configuration.hpp"
#include "../ErrorContext.hpp"
#include "../PropertyValue.hpp"

#include <stdlib.h>
#ifdef __linux__
#include <signal.h>
#endif

namespace {
  struct JitLoader {
    Psi::CompileErrorContext jit_error_context;
    boost::shared_ptr<Psi::Tvm::JitFactory> jit_factory;

    JitLoader()
    : jit_error_context(&std::cerr) {
      Psi::PropertyValue config;
      Psi::configuration_builtin(config);
      Psi::configuration_read_files(config);
      if (const char *s = std::getenv("PSI_TEST_CONFIG"))
        config.parse_file(s);
      config.get("tvm");
      jit_factory = Psi::Tvm::JitFactory::get(jit_error_context.bind(Psi::SourceLocation::root_location("(jit)")), config.path_value("tvm"));
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
      error_context(&std::cerr),
      context(&error_context),
      module(&context, "test_module", location),
      m_jit(jit_loader.jit_factory->create_jit()) {
        // Some versions of Boost.Test (I'm using 1.46) treat child processes exiting
        // with a nonzero status as a test failure. Ignoring SIGCHLD works around this.
        // Note that even though SIGCHLD is ignored by default SIG_DFL is distinct from
        // SIG_IGN here, because SIG_IGN causes child processes to be orphaned on creation
        // and thus the exit status is not available.
#ifdef __linux__
        signal(SIGCHLD, SIG_DFL);
#endif
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
