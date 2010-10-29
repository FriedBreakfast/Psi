#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class ReturnInsn {
    public:
      TermPtr<> type(Context& context, std::size_t n_parameters, Term *const* parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, InstructionTerm&) const;
      bool operator == (const ReturnInsn&) const;
      friend std::size_t hash_value(const ReturnInsn&);
    };
  }
}

#endif
