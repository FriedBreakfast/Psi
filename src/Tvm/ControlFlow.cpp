#include "Function.hpp"
#include "Functional.hpp"
#include "ControlFlow.hpp"
#include "Primitive.hpp"
#include "Number.hpp"
#include "Derived.hpp"
#include "LLVMBuilder.hpp"

#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    Term* Return::type(Context&, const FunctionTerm& function, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 1)
	throw std::logic_error("return instruction takes one argument");

      Term *ret_val = parameters[0];
      if (ret_val->type() != function.result_type())
	throw std::logic_error("return instruction argument has incorrect type");

      if (ret_val->phantom())
        throw std::logic_error("cannot return a phantom value");

      return NULL;
    }

    LLVMValue Return::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      LLVMIRBuilder& irbuilder = builder.irbuilder();

      Term* return_value = term.parameter(0);
      LLVMValue result = builder.value(return_value);

      if (builder.calling_convention() == cconv_tvm) {
	llvm::Value *return_area = &builder.function()->getArgumentList().front();
        builder.create_store(return_area, return_value);
        irbuilder.CreateRetVoid();
      } else {
	if (!result.is_known())
	  throw std::logic_error("Return value from a non-dependent function must have a known LLVM value");
	irbuilder.CreateRet(result.known_value());
      }

      return LLVMValue::empty();
    }

    void Return::jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const {
    }

    Term* ConditionalBranch::type(Context& context, const FunctionTerm&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 3)
	throw std::logic_error("branch instruction takes three arguments: cond, trueTarget, falseTarget");

      Term *cond = parameters[0];

      if (cond->type() != context.get_boolean_type())
	throw std::logic_error("first parameter to branch instruction must be of boolean type");

      Term* true_target(parameters[1]);
      Term* false_target(parameters[2]);
      if ((true_target->term_type() != term_block) || (false_target->term_type() != term_block))
	throw std::logic_error("second and third parameters to branch instruction must be blocks");

      PSI_ASSERT(!true_target->phantom() && !false_target->phantom());

      if (cond->phantom())
        throw std::logic_error("cannot conditionally branch on a phantom value");

      return NULL;
    }

    LLVMValue ConditionalBranch::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      LLVMValue cond = builder.value(self.condition());
      LLVMValue true_target = builder.value(self.true_target());
      LLVMValue false_target = builder.value(self.false_target());

      PSI_ASSERT(cond.is_known() && true_target.is_known() && false_target.is_known());

      llvm::Value *cond_llvm = cond.known_value();
      llvm::BasicBlock *true_target_llvm = llvm::cast<llvm::BasicBlock>(true_target.known_value());
      llvm::BasicBlock *false_target_llvm = llvm::cast<llvm::BasicBlock>(false_target.known_value());

      return LLVMValue::known(builder.irbuilder().CreateCondBr(cond_llvm, true_target_llvm, false_target_llvm));
    }

    void ConditionalBranch::jump_targets(Context&, InstructionTerm& term, std::vector<BlockTerm*>& targets) const {
      Access self(&term, this);
      targets.push_back(self.true_target());
      targets.push_back(self.false_target());
    }

    Term* UnconditionalBranch::type(Context&, const FunctionTerm&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 1)
	throw std::logic_error("unconditional branch instruction takes one argument - the branch target");

      Term* target(parameters[0]);
      if (target->term_type() != term_block)
	throw std::logic_error("second parameter to branch instruction must be blocks");

      PSI_ASSERT(!target->phantom());

      return NULL;
    }

    LLVMValue UnconditionalBranch::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      LLVMValue target = builder.value(self.target());

      PSI_ASSERT(target.is_known());
      llvm::BasicBlock *target_llvm = llvm::cast<llvm::BasicBlock>(target.known_value());
      return LLVMValue::known(builder.irbuilder().CreateBr(target_llvm));
    }

    void UnconditionalBranch::jump_targets(Context&, InstructionTerm& term, std::vector<BlockTerm*>& targets) const {
      Access self(&term, this);
      targets.push_back(self.target());
    }

    Term* FunctionCall::type(Context&, const FunctionTerm&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() < 1)
	throw std::logic_error("function call instruction must have at least one parameter: the function being called");

      Term *target = parameters[0];
      if (target->phantom())
        throw std::logic_error("function call target cannot have phantom value");

      FunctionalTermPtr<PointerType> target_ptr_type = dynamic_cast_functional<PointerType>(target->type());
      if (!target_ptr_type)
	throw std::logic_error("function call target is not a pointer type");

      FunctionTypeTerm* target_function_type = dynamic_cast<FunctionTypeTerm*>(target_ptr_type.backend().target_type());
      if (!target_function_type)
	throw std::logic_error("function call target is not a function pointer");

      std::size_t n_parameters = target_function_type->n_parameters();
      if (parameters.size() != n_parameters + 1)
	throw std::logic_error("wrong number of arguments to function");

      for (std::size_t i = 0; i < n_parameters; ++i) {
        if ((i >= target_function_type->n_phantom_parameters()) && parameters[i]->phantom())
          throw std::logic_error("cannot pass phantom value to non-phantom function parameter");

	Term* expected_type = target_function_type->parameter_type_after(parameters.slice(1, i+1));
	if (parameters[i+1]->type() != expected_type)
	  throw std::logic_error("function argument has the wrong type");
      }

      Term* result_type = target_function_type->result_type_after(parameters.slice(1, 1+n_parameters));
      if (result_type->phantom())
        throw std::logic_error("cannot create function call which leads to unknown result type");

      return result_type;
    }

    LLVMValue FunctionCall::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      LLVMIRBuilder& irbuilder = builder.irbuilder();
      Access self(&term, this);

      FunctionTypeTerm* function_type =
	checked_cast<FunctionTypeTerm*>
	(checked_cast_functional<PointerType>(self.target()->type()).backend().target_type());

      LLVMValue target = builder.value(self.target());
      PSI_ASSERT(target.is_known());
      LLVMType result_type = builder.type(term.type());

      std::size_t n_parameters = function_type->n_parameters();
      std::size_t n_phantom = function_type->n_phantom_parameters();
      CallingConvention calling_convention = function_type->calling_convention();

      llvm::Value *stack_backup = NULL;
      llvm::Value *result_area;

      std::vector<llvm::Value*> parameters;
      if (calling_convention == cconv_tvm) {
	// allocate an area of memory to receive the result value
	if (result_type.is_known()) {
	  // stack pointer is saved here but not for unknown types
	  // because memory for unknown types must survive their
	  // scope.
	  stack_backup = irbuilder.CreateCall(builder.llvm_stacksave());
          result_area = irbuilder.CreateAlloca(result_type.type());
	  parameters.push_back(builder.cast_pointer_to_generic(result_area));
	} else if (result_type.is_empty()) {
	  result_area = llvm::Constant::getNullValue(llvm::Type::getInt8Ty(builder.context()));
	  parameters.push_back(result_area);
	} else {
	  PSI_ASSERT(result_type.is_unknown());
          result_area = builder.create_alloca_for(term.type());
	  parameters.push_back(result_area);
	}
      }

      for (std::size_t i = n_phantom; i < n_parameters; ++i) {
	LLVMValue param = builder.value(self.parameter(i));

	if (calling_convention == cconv_tvm) {
	  if (param.is_known()) {
            if (!stack_backup)
              stack_backup = irbuilder.CreateCall(builder.llvm_stacksave());

            llvm::Value *ptr = irbuilder.CreateAlloca(param.known_value()->getType());
            irbuilder.CreateStore(param.known_value(), ptr);
            parameters.push_back(builder.cast_pointer_to_generic(ptr));
	  } else if (param.is_unknown()) {
	    parameters.push_back(param.unknown_value());
	  } else {
            PSI_ASSERT(param.is_empty());
            parameters.push_back(llvm::Constant::getNullValue(llvm::Type::getInt8PtrTy(builder.context())));
          }
	} else {
	  if (!param.is_known())
	    throw std::logic_error("Function parameter types must be known for non-TVM calling conventions");
	  parameters.push_back(builder.cast_pointer_to_generic(param.known_value()));
	}
      }

      LLVMType llvm_function_type = builder.type(function_type);
      PSI_ASSERT(llvm_function_type.is_known());

      llvm::Value *llvm_target = builder.cast_pointer_from_generic(target.known_value(), llvm_function_type.type()->getPointerTo());
      llvm::Value *result = irbuilder.CreateCall(llvm_target, parameters.begin(), parameters.end());

      if ((calling_convention == cconv_tvm)  && result_type.is_known())
        result = irbuilder.CreateLoad(result_area);

      if (stack_backup)
	irbuilder.CreateCall(builder.llvm_stackrestore(), stack_backup);

      if (result_type.is_known()) {
	return LLVMValue::known(result);
      } else if (result_type.is_empty()) {
	return LLVMValue::empty();
      } else {
	PSI_ASSERT(result_type.is_unknown());
	return LLVMValue::unknown(result_area);
      }
    }
    
    void FunctionCall::jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const {
    }

    FunctionalTypeResult FunctionApplyPhantom::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() < 1)
        throw std::logic_error("apply_phantom requires at least one parameter");

      std::size_t n_applied = parameters.size() - 1;

      Term *target = parameters[0];
      FunctionalTermPtr<PointerType> target_ptr_type = dynamic_cast_functional<PointerType>(target->type());
      if (!target_ptr_type)
	throw std::logic_error("apply_phantom target is not a function pointer");

      FunctionTypeTerm* function_type = dynamic_cast<FunctionTypeTerm*>(target_ptr_type.backend().target_type());
      if (!function_type)
	throw std::logic_error("apply_phantom target is not a function pointer");

      if (n_applied > function_type->n_phantom_parameters())
        throw std::logic_error("Too many parameters given to apply_phantom");

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

      return FunctionalTypeResult(context.get_pointer_type(result_function_type).get(), parameters[0]->phantom());
    }

    LLVMType FunctionApplyPhantom::llvm_type(LLVMValueBuilder&, Term&) const {
      throw std::logic_error("the type of a term cannot be an apply_phantom");
    }

    LLVMValue FunctionApplyPhantom::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);
      return builder.value(self.function());
    }

    LLVMValue FunctionApplyPhantom::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);
      return builder.value(self.function());
    }
  }
}
