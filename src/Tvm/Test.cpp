#include "Test.hpp"
#include "Assembler.hpp"

#include <cstdlib>
#include <iostream>

#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/Module.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Target/TargetOptions.h>

namespace Psi {
  namespace Tvm {
    namespace Test {
      ContextFixture::ContextFixture()
        : m_debug_listener(test_env("PSI_TEST_DUMP_LLVM"),
                           test_env("PSI_TEST_DUMP_ASM")) {

        const char *emit_debug = std::getenv("PSI_TEST_DEBUG");
        if (emit_debug && (std::strcmp(emit_debug, "1") == 0))
            llvm::JITEmitDebugInfo = true;

        context.register_llvm_jit_listener(&m_debug_listener);
      }

      ContextFixture::~ContextFixture() {
      }

      bool ContextFixture::test_env(const char *name) {
        const char *value = std::getenv(name);
        if (value && (std::strcmp(value, "1") == 0))
          return true;
        return false;
      }

      /**
       * JIT compile some assembler code and return the value of a single symbol.
       */
      void* ContextFixture::jit_single(const char *name, const char *src) {
        AssemblerResult r = parse_and_build(context, src);
        AssemblerResult::iterator it = r.find(name);
        BOOST_REQUIRE(it != r.end());
        void *result = context.term_jit(it->second);
        return result;
      }

      DebugListener::DebugListener(bool dump_llvm, bool dump_asm)
        : m_dump_llvm(dump_llvm), m_dump_asm(dump_asm) {
      }

      void DebugListener::NotifyFunctionEmitted (const llvm::Function &F, void*, size_t, const EmittedFunctionDetails& details) {
        llvm::raw_os_ostream out(std::cerr);
        if (m_dump_llvm)
          F.print(out);
        if (m_dump_asm)
          details.MF->print(out);
      }
    }
  }
}
