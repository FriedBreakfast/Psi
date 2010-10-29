#include "Function.hpp"
#include "Instruction.hpp"
#include "LLVMBuilder.hpp"

#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    TermPtr<> ReturnInsn::type(Context& context, std::size_t n_parameters, Term *const* parameters) const {
      if (n_parameters != 1)
	throw std::logic_error("return instruction takes one argument");

      Term *ret_val = parameters[0];
      // Need to check return value has correct type
      throw std::logic_error("not implemented");
    }

    LLVMValue ReturnInsn::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      LLVMFunctionBuilder::IRBuilder& irbuilder = builder.irbuilder();

      TermPtr<> return_value = term.parameter(0);
      LLVMValue result = builder.value(return_value);

      if (builder.calling_convention() == cconv_tvm) {
	llvm::Value *return_area = &builder.function()->getArgumentList().front();
	if (result.is_known()) {
	  llvm::Value *cast_return_area = irbuilder.CreateBitCast(return_area, llvm::Type::getInt8PtrTy(builder.context())->getPointerTo());
	  irbuilder.CreateStore(cast_return_area, result.value());
	  if (result.value()->getType()->isPointerTy()) {
	    return LLVMValue::known(irbuilder.CreateRet(result.value()));
	  } else {
	    return LLVMValue::known(irbuilder.CreateRet(return_area));
	  }
	} else if (result.is_empty()) {
	  return LLVMValue::known(irbuilder.CreateRet(return_area));
	} else {
	  llvm::Function *memcpy_fn = llvm_intrinsic_memcpy(builder.module());
	  term.type();
	  irbuilder.CreateCall5(memcpy_fn, return_area, result.ptr_value(), NULL, NULL, llvm::ConstantInt::getFalse(builder.context()));
	  return LLVMValue::known(irbuilder.CreateRet(return_area));
	}
      } else {
	if (!result.is_known())
	  throw std::logic_error("Return value from a non-dependent function must have a known LLVM value");
	return LLVMValue::known(irbuilder.CreateRet(result.value()));
      }
    }

    bool ReturnInsn::operator == (const ReturnInsn&) const {
      return true;
    }

    std::size_t hash_value(const ReturnInsn&) {
      return 0;
    }
  }
}
