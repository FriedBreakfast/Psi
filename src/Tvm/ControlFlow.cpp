#include "Function.hpp"
#include "Functional.hpp"
#include "ControlFlow.hpp"
#include "Primitive.hpp"
#include "Number.hpp"
#include "Type.hpp"
#include "LLVMBuilder.hpp"

#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <stdexcept>

namespace Psi {
  namespace Tvm {

    void FunctionCall::jump_targets(Ptr, std::vector<BlockTerm*>&) {
    }

    FunctionalTypeResult FunctionSpecialize::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() < 1)
        throw TvmUserError("apply_phantom requires at least one parameter");

      std::size_t n_applied = parameters.size() - 1;

      Term *target = parameters[0];
      PointerType::Ptr target_ptr_type = dyn_cast<PointerType>(target->type());
      if (!target_ptr_type)
	throw TvmUserError("apply_phantom target is not a function pointer");

      FunctionTypeTerm* function_type = dyn_cast<FunctionTypeTerm>(target_ptr_type->target_type());
      if (!function_type)
	throw TvmUserError("apply_phantom target is not a function pointer");

      if (n_applied > function_type->n_phantom_parameters())
        throw TvmUserError("Too many parameters given to apply_phantom");

      ScopedTermPtrArray<> apply_parameters(function_type->n_parameters());
      for (std::size_t i = 0; i < n_applied; ++i)
        apply_parameters[i] = parameters[i+1];

      ScopedTermPtrArray<FunctionTypeParameterTerm> new_parameters(function_type->n_parameters() - n_applied);
      for (std::size_t i = 0; i < new_parameters.size(); ++i) {
        Term* type = function_type->parameter_type_after(apply_parameters.array().slice(0, n_applied + i));
        FunctionTypeParameterTerm* param = context.new_function_type_parameter(type);
        apply_parameters[i + n_applied] = param;
        new_parameters[i] = param;
      }

      Term* result_type = function_type->result_type_after(apply_parameters.array());

      std::size_t result_n_phantom = function_type->n_phantom_parameters() - n_applied;
      std::size_t result_n_normal = function_type->n_parameters() - function_type->n_phantom_parameters();

      FunctionTypeTerm* result_function_type = context.get_function_type
        (function_type->calling_convention(),
         result_type,
         new_parameters.array().slice(0, result_n_phantom),
         new_parameters.array().slice(result_n_phantom, result_n_phantom+result_n_normal));

      return FunctionalTypeResult(PointerType::get(result_function_type), parameters[0]->phantom());
    }

#if 0
    LLVMValue Return::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) {
      LLVMIRBuilder& irbuilder = builder.irbuilder();

      Term* return_value = term.parameter(0);
      LLVMValue result = builder.build_value(return_value);

      if (builder.function()->function_type()->calling_convention() == cconv_tvm) {
	llvm::Value *return_area = &builder.llvm_function()->getArgumentList().front();
        builder.create_store(return_area, return_value);
        irbuilder.CreateRetVoid();
      } else {
	if (!result.is_known())
	  throw LLVMBuildError("Return value from a non-dependent function must have a known LLVM value");
	irbuilder.CreateRet(result.known_value());
      }

      return LLVMValue::known(EmptyType::llvm_empty_value(builder));
    }

    LLVMValue ConditionalBranch::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) {
      Access self(&term, this);
      LLVMValue cond = builder.build_value(self.condition());
      LLVMValue true_target = builder.build_value(self.true_target());
      LLVMValue false_target = builder.build_value(self.false_target());

      PSI_ASSERT(cond.is_known() && true_target.is_known() && false_target.is_known());

      llvm::Value *cond_llvm = cond.known_value();
      llvm::BasicBlock *true_target_llvm = llvm::cast<llvm::BasicBlock>(true_target.known_value());
      llvm::BasicBlock *false_target_llvm = llvm::cast<llvm::BasicBlock>(false_target.known_value());
      builder.irbuilder().CreateCondBr(cond_llvm, true_target_llvm, false_target_llvm);

      return LLVMValue::known(EmptyType::llvm_empty_value(builder));
    }

    LLVMValue UnconditionalBranch::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) {
      Access self(&term, this);
      LLVMValue target = builder.build_value(self.target());

      PSI_ASSERT(target.is_known());
      llvm::BasicBlock *target_llvm = llvm::cast<llvm::BasicBlock>(target.known_value());
      builder.irbuilder().CreateBr(target_llvm);

      return LLVMValue::known(EmptyType::llvm_empty_value(builder));
    }

    LLVMValue FunctionCall::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) {
      LLVMIRBuilder& irbuilder = builder.irbuilder();
      Access self(&term, this);

      FunctionTypeTerm* function_type = cast<FunctionTypeTerm>
        (cast<PointerType>(self.target()->type())->target_type());

      LLVMValue target = builder.build_value(self.target());
      PSI_ASSERT(target.is_known());
      const llvm::Type* result_type = builder.build_type(term.type());

      std::size_t n_parameters = function_type->n_parameters();
      std::size_t n_phantom = function_type->n_phantom_parameters();
      CallingConvention calling_convention = function_type->calling_convention();

      llvm::Value *stack_backup = NULL;
      llvm::Value *result_area;

      std::vector<llvm::Value*> parameters;
      if (calling_convention == cconv_tvm) {
	// allocate an area of memory to receive the result value
	if (result_type) {
	  // stack pointer is saved here but not for unknown types
	  // because memory for unknown types must survive their
	  // scope.
	  stack_backup = irbuilder.CreateCall(LLVMIntrinsics::stacksave(builder.llvm_module()));
          result_area = irbuilder.CreateAlloca(result_type);
	  parameters.push_back(builder.cast_pointer_to_generic(result_area));
	} else {
          result_area = builder.create_alloca_for(term.type());
	  parameters.push_back(result_area);
	}
      }

      const llvm::FunctionType* llvm_function_type =
        llvm::cast<llvm::FunctionType>(builder.build_type(function_type));
      if (!llvm_function_type)
        throw LLVMBuildError("cannot call function of unknown type");

      for (std::size_t i = n_phantom; i < n_parameters; ++i) {
	LLVMValue param = builder.build_value(self.parameter(i));

	if (calling_convention == cconv_tvm) {
	  if (param.is_known()) {
            if (!stack_backup)
              stack_backup = irbuilder.CreateCall(LLVMIntrinsics::stacksave(builder.llvm_module()));

            llvm::Value *ptr = irbuilder.CreateAlloca(param.known_value()->getType());
            irbuilder.CreateStore(param.known_value(), ptr);
            parameters.push_back(builder.cast_pointer_to_generic(ptr));
	  } else {
            PSI_ASSERT(param.is_unknown());
	    parameters.push_back(param.unknown_value());
	  }
	} else {
	  if (!param.is_known())
	    throw LLVMBuildError("Function parameter types must be known for non-TVM calling conventions");
          llvm::Value *val = param.known_value();
          if (val->getType()->isPointerTy())
            val = builder.cast_pointer_from_generic(val, llvm_function_type->getParamType(i));
	  parameters.push_back(val);
	}
      }

      llvm::Value *llvm_target = builder.cast_pointer_from_generic(target.known_value(), llvm_function_type->getPointerTo());
      llvm::Value *result = irbuilder.CreateCall(llvm_target, parameters.begin(), parameters.end());

      if ((calling_convention == cconv_tvm)  && result_type)
        result = irbuilder.CreateLoad(result_area);

      if (stack_backup)
	irbuilder.CreateCall(LLVMIntrinsics::stackrestore(builder.llvm_module()), stack_backup);

      if (result_type) {
	return LLVMValue::known(result);
      } else {
	return LLVMValue::unknown(result_area);
      }
    }

    LLVMValue FunctionSpecialize::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) {
      Access self(&term, this);
      return builder.build_value(self.function());
    }

    llvm::Constant* FunctionSpecialize::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) {
      Access self(&term, this);
      return builder.build_constant(self.function());
    }
#endif
  }
}
