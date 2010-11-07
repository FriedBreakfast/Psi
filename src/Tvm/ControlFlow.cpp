#include "Function.hpp"
#include "Functional.hpp"
#include "ControlFlow.hpp"
#include "Primitive.hpp"
#include "Number.hpp"
#include "LLVMBuilder.hpp"

#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    TermPtr<> Return::type(Context& context, const FunctionTerm& function, TermRefArray<> parameters) const {
      if (parameters.size() != 1)
	throw std::logic_error("return instruction takes one argument");

      Term *ret_val = parameters[0];
      if (ret_val->type() != function.result_type())
	throw std::logic_error("return instruction argument has incorrect type");

      return context.get_empty_type();
    }

    LLVMValue Return::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
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

    TermPtr<> ConditionalBranch::type(Context& context, const FunctionTerm&, TermRefArray<> parameters) const {
      if (parameters.size() != 3)
	throw std::logic_error("branch instruction takes three arguments: cond, trueTarget, falseTarget");

      Term *cond = parameters[0];

      if (cond->type() != context.get_boolean_type())
	throw std::logic_error("first parameter to branch instruction must be of boolean type");

      Term *true_target = parameters[1];
      Term *false_target = parameters[2];
      if ((true_target->type() != context.get_block_type()) || (false_target->type() != context.get_block_type()))
	throw std::logic_error("second and third parameters to branch instruction must be blocks");

      return context.get_empty_type();
    }

    LLVMValue ConditionalBranch::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      LLVMValue cond = builder.value(self.condition());
      LLVMValue true_target = builder.value(self.true_target());
      LLVMValue false_target = builder.value(self.false_target());

      if (!cond.is_known() || !true_target.is_known() || !false_target.is_known())
	throw std::logic_error("all parameters to branch instruction must have known value");

      llvm::Value *cond_llvm = cond.value();
      llvm::BasicBlock *true_target_llvm = llvm::cast<llvm::BasicBlock>(true_target.value());
      llvm::BasicBlock *false_target_llvm = llvm::cast<llvm::BasicBlock>(false_target.value());

      return LLVMValue::known(builder.irbuilder().CreateCondBr(cond_llvm, true_target_llvm, false_target_llvm));
    }

    TermPtr<> UnconditionalBranch::type(Context& context, const FunctionTerm&, TermRefArray<> parameters) const {
      if (parameters.size() != 1)
	throw std::logic_error("unconditional branch instruction takes one argument - the branch target");

      Term *target = parameters[0];
      if (target->type() != context.get_block_type())
	throw std::logic_error("second and third parameters to branch instruction must be blocks");

      return context.get_empty_type();
    }

    LLVMValue UnconditionalBranch::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      LLVMValue target = builder.value(self.target());

      if (!target.is_known())
	throw std::logic_error("parameter to unconditional branch instruction must have known value");

      llvm::BasicBlock *target_llvm = llvm::cast<llvm::BasicBlock>(target.value());

      return LLVMValue::known(builder.irbuilder().CreateBr(target_llvm));
    }
  }
}
