#include "Test.hpp"
#include "Assembler.hpp"

#include <cstdlib>
#include <iostream>

namespace Psi {
  namespace Tvm {
    namespace Test {
#if 0
      class ContextFixture::DebugListener : public llvm::JITEventListener {
      public:
        DebugListener(bool dump_llvm, bool dump_asm)
          : m_dump_llvm(dump_llvm), m_dump_asm(dump_asm) {
        }

        virtual void NotifyFunctionEmitted (const llvm::Function &F, void*, size_t, const EmittedFunctionDetails& details) {
          llvm::raw_os_ostream out(std::cerr);
          if (m_dump_llvm)
            F.print(out);
          if (m_dump_asm)
            details.MF->print(out);
        }

        //virtual void NotifyFreeingMachineCode (void *OldPtr)

      private:
        bool m_dump_llvm;
        bool m_dump_asm;
      };
#endif

      ContextFixture::ContextFixture() {}
#if 0
        : m_jit(create_llvm_jit(&context)),
          m_debug_listener(new DebugListener(test_env("PSI_TEST_DUMP_LLVM"),
                                             test_env("PSI_TEST_DUMP_ASM"))) {

        const char *emit_debug = std::getenv("PSI_TEST_DEBUG");
        if (emit_debug && (std::strcmp(emit_debug, "1") == 0))
            llvm::JITEmitDebugInfo = true;

        boost::shared_ptr<LLVM::LLVMJit> llvm_jit = boost::static_pointer_cast<LLVM::LLVMJit>(m_jit);
        llvm_jit->register_llvm_jit_listener(m_debug_listener.get());
      }
#endif

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
        void *result = m_jit->get_global(it->second);
        return result;
      }
    }
  }
}
