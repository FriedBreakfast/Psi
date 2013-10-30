#include <iostream>

#include "Test.hpp"
#include "Assembler.hpp"
#include "../Platform/Platform.hpp"
#include "../Configuration.hpp"
#include "../ErrorContext.hpp"
#include "../PropertyValue.hpp"

#include <stdlib.h>

namespace {
  struct JitLoader {
    Psi::CompileErrorContext jit_error_context;
    boost::shared_ptr<Psi::Tvm::JitFactory> jit_factory;

    JitLoader()
    : jit_error_context(&std::cerr) {
      Psi::PropertyValue config;
      Psi::configuration_builtin(config);
      Psi::configuration_read_files(config);
      Psi::configuration_environment(config);
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
  
          LogicalSourceLocationPtr log = LogicalSourceLocation::new_root();
          return SourceLocation(phys, log);
        }
      }
      
      ContextFixture::ContextFixture()
      : location(module_location()),
      error_context(&std::cerr),
      context(&error_context),
      module(&context, "test_module", location),
      m_jit(jit_loader.jit_factory->create_jit()) {
      }

      ContextFixture::~ContextFixture() {
      }

      /**
       * JIT compile some assembler code and return the value of a single symbol.
       */
      void* ContextFixture::jit_single(const char *name, const char *src) {
        AssemblerResult r = parse_and_build(module, location.physical, src);
        AssemblerResult::iterator it = r.find(name);
        PSI_TEST_REQUIRE(it != r.end());
        m_jit->add_module(&module);
        void *result = m_jit->get_symbol(value_cast<Global>(it->second));
        return result;
      }
    }
  }
}
