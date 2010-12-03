#include "Test.hpp"
#include "Assembler.hpp"

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

      void DebugListener::NotifyFunctionEmitted (const llvm::Function &F, void*, size_t, const EmittedFunctionDetails& details) {
        llvm::raw_os_ostream out(std::cerr);
        F.print(out);
        details.MF->print(out);
      }
    }
  }
}
