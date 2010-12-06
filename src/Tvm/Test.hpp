#ifndef HPP_PSI_TVM_TEST
#define HPP_PSI_TVM_TEST

#include "Core.hpp"

#include <boost/test/unit_test.hpp>

#include <llvm/ExecutionEngine/JITEventListener.h>

namespace Psi {
  namespace Tvm {
    namespace Test {
      class DebugListener : public llvm::JITEventListener {
      public:
        DebugListener(bool dump_llvm, bool dump_asm);

        virtual void NotifyFunctionEmitted (const llvm::Function &F, void *Code, std::size_t Size, const EmittedFunctionDetails &Details);

        //virtual void NotifyFreeingMachineCode (void *OldPtr)

      private:
        bool m_dump_llvm;
        bool m_dump_asm;
      };

      class ContextFixture {
      public:
        Context context;

        ContextFixture();
        ~ContextFixture();

        static bool test_env(const char *name);
        void* jit_single(const char *name, const char *src);

      private:
        DebugListener m_debug_listener;
      };
    }
  }
}

#endif
