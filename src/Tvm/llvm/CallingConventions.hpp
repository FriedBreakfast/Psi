#ifndef HPP_PSI_TVM_LLVM_CALLING_CONVENTIONS
#define HPP_PSI_TVM_LLVM_CALLING_CONVENTIONS

#include "../AggregateLowering.hpp"
#include "../../Utility.hpp"

#include "LLVMPushWarnings.hpp"
#include <llvm/ADT/Triple.h>
#include "LLVMPopWarnings.hpp"

namespace Psi {
namespace Tvm {
namespace LLVM {
/**
 * \brief Base class for implementing calling conventions
 */
class CallingConventionHandler {
public:
  virtual ~CallingConventionHandler();
  virtual void lower_function_call(AggregateLoweringPass::FunctionRunner&, const ValuePtr<Call>&) = 0;
  virtual ValuePtr<Instruction> lower_return(AggregateLoweringPass::FunctionRunner&, const ValuePtr<>&, const SourceLocation&) = 0;
  virtual ValuePtr<Function> lower_function(AggregateLoweringPass&, const ValuePtr<Function>&) = 0;
  virtual void lower_function_entry(AggregateLoweringPass::FunctionRunner&, const ValuePtr<Function>&, const ValuePtr<Function>&) = 0;
};

void calling_convention_handler(const CompileErrorPair& error_loc, llvm::Triple target, CallingConvention cc, UniquePtr<CallingConventionHandler>& result);
}
}
}

#endif
