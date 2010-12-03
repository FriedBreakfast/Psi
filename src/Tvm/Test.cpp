#include "Test.hpp"

#include <cstdlib>
#include <iostream>

#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/Module.h>
#include <llvm/Support/raw_os_ostream.h>

namespace Psi {
  namespace Tvm {
    namespace Test {
      ContextFixture::ContextFixture() {
        const char *dump_llvm = std::getenv("PSI_TEST_DUMP_LLVM");

        if (dump_llvm && (std::strcmp(dump_llvm, "1") == 0))
          context.register_llvm_jit_listener(&m_debug_listener);
      }

      ContextFixture::~ContextFixture() {
      }

      void DebugListener::NotifyFunctionEmitted (const llvm::Function &F, void*, size_t, const EmittedFunctionDetails& details) {
        llvm::raw_os_ostream out(std::cerr);
        F.print(out);
        details.MF->print(out);
      }
    }
  }
}
