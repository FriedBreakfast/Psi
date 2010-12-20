#include "Function.hpp"
#include "Derived.hpp"
#include "Memory.hpp"
#include "Primitive.hpp"
#include "LLVMBuilder.hpp"

#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    LLVMValue Store::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      llvm::Value *target = builder.build_known_value(self.target());
      builder.create_store(target, self.value());
      return LLVMValue::known(EmptyType::llvm_empty_value(builder));
    }

    LLVMValue Load::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);

      llvm::Value *target = builder.build_known_value(self.target());

      Term *target_deref_type = cast<PointerType>(self.target()->type())->target_type();
      if (const llvm::Type *llvm_target_deref_type = builder.build_type(target_deref_type)) {
        llvm::Value *ptr = builder.cast_pointer_from_generic(target, llvm_target_deref_type->getPointerTo());
        return LLVMValue::known(builder.irbuilder().CreateLoad(ptr));
      } else {
        llvm::Value *stack_ptr = builder.create_alloca_for(target_deref_type);
        builder.create_store_unknown(stack_ptr, target, target_deref_type);
        return LLVMValue::unknown(stack_ptr);
      }
    }

    LLVMValue Alloca::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      return LLVMValue::known(builder.create_alloca_for(self.stored_type()));
    }
  }
}
