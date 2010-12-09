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
    Term* Store::type(Context& context, const FunctionTerm&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 2)
        throw TvmUserError("store instruction takes two parameters");

      Term *value = parameters[0];
      Term *target = parameters[1];

      if (target->phantom() || value->phantom())
        throw TvmUserError("value and target for store instruction cannot have phantom values");

      FunctionalTermPtr<PointerType> target_ptr_type = dynamic_cast_functional<PointerType>(target->type());
      if (!target_ptr_type)
	throw TvmUserError("store target is not a pointer type");

      if (target_ptr_type.backend().target_type() != value->type())
        throw TvmUserError("store target type is not a pointer to the type of value");

      return context.get_empty_type();
    }

    LLVMValue Store::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      llvm::Value *target = builder.build_known_value(self.target());
      builder.create_store(target, self.value());
      return LLVMValue::known(EmptyType::llvm_empty_value(builder.llvm_context()));
    }

    void Store::jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const {
    }

    Term* Load::type(Context&, const FunctionTerm&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 1)
        throw TvmUserError("load instruction takes one parameter");

      Term *target = parameters[0];

      if (target->phantom())
        throw TvmUserError("value and target for load instruction cannot have phantom values");

      FunctionalTermPtr<PointerType> target_ptr_type = dynamic_cast_functional<PointerType>(target->type());
      if (!target_ptr_type)
	throw TvmUserError("load target is not a pointer type");

      if (target_ptr_type.backend().target_type()->phantom())
        throw TvmUserError("load target has phantom type");

      return target_ptr_type.backend().target_type();
    }

    LLVMValue Load::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);

      llvm::Value *target = builder.build_known_value(self.target());

      Term *target_deref_type = checked_cast_functional<PointerType>(self.target()->type()).backend().target_type();
      if (const llvm::Type *llvm_target_deref_type = builder.build_type(target_deref_type)) {
        llvm::Value *ptr = builder.cast_pointer_from_generic(target, llvm_target_deref_type->getPointerTo());
        return LLVMValue::known(builder.irbuilder().CreateLoad(ptr));
      } else {
        llvm::Value *stack_ptr = builder.create_alloca_for(target_deref_type);
        builder.create_store_unknown(stack_ptr, target, target_deref_type);
        return LLVMValue::unknown(stack_ptr);
      }
    }

    void Load::jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const {
    }

    Term* Alloca::type(Context& context, const FunctionTerm&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 1)
        throw TvmUserError("alloca instruction takes one parameter");

      if (!parameters[0]->is_type())
        throw TvmUserError("parameter to alloca is not a type");

      if (parameters[0]->phantom())
        throw TvmUserError("parameter to alloca cannot be phantom");

      return context.get_pointer_type(parameters[0]);
    }

    LLVMValue Alloca::llvm_value_instruction(LLVMFunctionBuilder& builder, InstructionTerm& term) const {
      Access self(&term, this);
      return LLVMValue::known(builder.create_alloca_for(self.stored_type()));
    }

    void Alloca::jump_targets(Context&, InstructionTerm&, std::vector<BlockTerm*>&) const {
    }
  }
}
