#include "Function.hpp"
#include "Functional.hpp"
#include "Instruction.hpp"
#include "LLVMBuilder.hpp"

#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    TermPtr<> ReturnInsn::type(Context& context, const FunctionTerm& function, TermRefArray<> parameters) const {
      if (parameters.size() != 1)
	throw std::logic_error("return instruction takes one argument");

      Term *ret_val = parameters[0];
      if (ret_val->type() != function.result_type())
	throw std::logic_error("return instruction argument has incorrect type");

      return context.get_empty_type();
    }

    LLVMValue ReturnInsn::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      LLVMFunctionBuilder::IRBuilder& irbuilder = builder.irbuilder();

      TermPtr<> return_value = term.parameter(0);
      LLVMValue result = builder.value(return_value);

      if (builder.calling_convention() == cconv_tvm) {
	llvm::Value *return_area = &builder.function()->getArgumentList().front();
	if (result.is_known()) {
	  llvm::Value *cast_return_area = irbuilder.CreateBitCast(return_area, result.value()->getType()->getPointerTo());
	  irbuilder.CreateStore(result.value(), cast_return_area);
	  if (result.value()->getType()->isPointerTy()) {
	    return LLVMValue::known(irbuilder.CreateRet(result.value()));
	  } else {
	    return LLVMValue::known(irbuilder.CreateRet(return_area));
	  }
	} else if (result.is_empty()) {
	  return LLVMValue::known(irbuilder.CreateRet(return_area));
	} else if (result.is_unknown()) {
	  llvm::Function *memcpy_fn = llvm_intrinsic_memcpy(builder.module());
	  TermPtr<> return_type = return_value->type();
	  if (return_type->type() != term.context().get_metatype())
	    throw std::logic_error("Type of return type is not metatype");

	  LLVMValue type_value = builder.value(return_type);
	  if (!type_value.is_known())
	    throw std::logic_error("Cannot return a value whose size and alignment is not known");

	  llvm::Value *size = irbuilder.CreateExtractValue(type_value.value(), 0);
	  // LLVM intrinsic memcpy requires that the alignment
	  // specified is a constant.
	  llvm::Value *align = llvm::ConstantInt::get(llvm::Type::getInt32Ty(builder.context()), 0);

	  irbuilder.CreateCall5(memcpy_fn, return_area, result.ptr_value(),
				size, align,
				llvm::ConstantInt::getFalse(builder.context()));
	  return LLVMValue::known(irbuilder.CreateRet(return_area));
	} else {
	  PSI_ASSERT(result.is_quantified());
	  throw std::logic_error("Cannot return a quantified value!");
	}
      } else {
	if (!result.is_known())
	  throw std::logic_error("Return value from a non-dependent function must have a known LLVM value");
	return LLVMValue::known(irbuilder.CreateRet(result.value()));
      }
    }
  }
}
