#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class ReturnInsn {
    public:
      TermPtr<> type(Context& context, const FunctionTerm& function, TermRefArray<> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
    };
  }
}

#endif
