#include "Builder.hpp"

#include "../Aggregate.hpp"
#include "../Instructions.hpp"
#include "../TermOperationMap.hpp"

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Function.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct InstructionBuilder {
        static llvm::Instruction* return_callback(FunctionBuilder& builder, const ValuePtr<Return>& insn) {
          return builder.irbuilder().CreateRet(builder.build_value(insn->value));
        }

        static llvm::Instruction* conditional_branch_callback(FunctionBuilder& builder, const ValuePtr<ConditionalBranch>& insn) {
          llvm::Value *cond = builder.build_value(insn->condition);
          llvm::BasicBlock *true_target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->true_target));
          llvm::BasicBlock *false_target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->false_target));
          return builder.irbuilder().CreateCondBr(cond, true_target, false_target);
        }

        static llvm::Instruction* unconditional_branch_callback(FunctionBuilder& builder, const ValuePtr<UnconditionalBranch>& insn) {
          llvm::BasicBlock *target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->target));
          return builder.irbuilder().CreateBr(target);
        }
        
        static llvm::Instruction* unreachable_callback(FunctionBuilder& builder, const ValuePtr<Unreachable>&) {
          return builder.irbuilder().CreateUnreachable();
        }

        static llvm::Instruction* function_call_callback(FunctionBuilder& builder, const ValuePtr<Call>& insn) {
          // Prepare target pointer
          ValuePtr<FunctionType> function_type = value_cast<FunctionType>
            (value_cast<PointerType>(insn->target->type())->target_type());
          llvm::Type *llvm_function_type = builder.module_builder()->build_type(function_type)->getPointerTo();
          llvm::Value *target = builder.build_value(insn->target);
          llvm::Value *cast_target = builder.irbuilder().CreatePointerCast(target, llvm_function_type);

          // Prepare parameters
          std::size_t n_phantom = function_type->n_phantom();
          std::size_t n_passed_parameters = function_type->parameter_types().size() - n_phantom;

          llvm::SmallVector<llvm::Value*, 4> parameters;
          parameters.resize(n_passed_parameters);
          for (std::size_t i = 0; i < n_passed_parameters; ++i)
            parameters[i] = builder.build_value(insn->parameters[i + n_phantom]);
          
          return builder.irbuilder().CreateCall(cast_target, parameters);
        }
        
        static llvm::Instruction* load_callback(FunctionBuilder& builder, const ValuePtr<Load>& term) {
          llvm::Value *target = builder.build_value(term->target);
          return builder.irbuilder().CreateLoad(target);
        }

        static llvm::Instruction* store_callback(FunctionBuilder& builder, const ValuePtr<Store>& term) {
          llvm::Value *target = builder.build_value(term->target);
          llvm::Value *value = builder.build_value(term->value);
          return builder.irbuilder().CreateStore(value, target);
        }

        static llvm::Instruction* alloca_callback(FunctionBuilder& builder, const ValuePtr<Alloca>& term) {
          llvm::Type *stored_type = builder.module_builder()->build_type(term->element_type);
          llvm::Value *count = term->count ? builder.build_value(term->count) : NULL;
          llvm::Value *alignment = term->alignment ? builder.build_value(term->alignment) : NULL;
          llvm::AllocaInst *inst = builder.irbuilder().CreateAlloca(stored_type, count);
          
          if (alignment) {
            if (llvm::ConstantInt *const_alignment = llvm::dyn_cast<llvm::ConstantInt>(alignment)) {
              inst->setAlignment(const_alignment->getValue().getZExtValue());
            } else {
              inst->setAlignment(builder.unknown_alloca_align());
            }
          }
          
          return inst;
        }
        
        static llvm::Instruction* stack_save_callback(FunctionBuilder& builder, const ValuePtr<StackSave>&) {
          return builder.irbuilder().CreateCall(builder.module_builder()->llvm_stacksave());
        }
        
        static llvm::Instruction* stack_restore_callback(FunctionBuilder& builder, const ValuePtr<StackRestore>& term) {
          llvm::Value *arg = builder.build_value(term->save);
          return builder.irbuilder().CreateCall(builder.module_builder()->llvm_stackrestore(), arg);
        }
        
        static llvm::Instruction* evaluate_callback(FunctionBuilder& builder, const ValuePtr<Evaluate>& term) {
          builder.build_value(term->value);
          return NULL;
        }
        
        static llvm::Instruction* memcpy_callback(FunctionBuilder& builder, const ValuePtr<MemCpy>& term) {
          llvm::Value *dest = builder.build_value(term->dest);
          llvm::Value *src = builder.build_value(term->src);
          llvm::Value *count = builder.build_value(term->count);
          unsigned alignment = 0;
          if (llvm::ConstantInt *alignment_expr = llvm::dyn_cast<llvm::ConstantInt>(builder.build_value(term->alignment)))
            alignment = alignment_expr->getValue().getZExtValue();
          
          PSI_ASSERT(dest->getType() == src->getType());

          llvm::Type *i8ptr = llvm::IntegerType::getInt8PtrTy(builder.module_builder()->llvm_context());
          if (dest->getType() != i8ptr) {
            const llvm::DataLayout *target_data = builder.module_builder()->llvm_target_machine()->getDataLayout();
            llvm::Type *element_type = llvm::cast<llvm::PointerType>(dest->getType())->getElementType();
            llvm::Constant *target_size = llvm::ConstantInt::get(target_data->getIntPtrType(builder.module_builder()->llvm_context()), target_data->getTypeAllocSize(element_type));
            count = builder.irbuilder().CreateMul(count, target_size);
            alignment = std::max(alignment, target_data->getABITypeAlignment(element_type));
            
            dest = builder.irbuilder().CreateBitCast(dest, i8ptr);
            src = builder.irbuilder().CreateBitCast(src, i8ptr);
          }
          
          llvm::ConstantInt *alignment_expr = llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(builder.module_builder()->llvm_context()), alignment);
          llvm::Value *isvolatile = llvm::ConstantInt::getFalse(builder.module_builder()->llvm_context());
          
          return builder.irbuilder().CreateCall5(builder.module_builder()->llvm_memcpy(), dest, src, count, alignment_expr, isvolatile);
        }
        
        static llvm::Instruction* memzero_callback(FunctionBuilder& builder, const ValuePtr<MemZero>& term) {
          llvm::Value *dest = builder.build_value(term->dest);
          llvm::Value *count = builder.build_value(term->count);
          unsigned alignment = 0;
          if (term->alignment) {
            if (llvm::ConstantInt *alignment_expr = llvm::dyn_cast<llvm::ConstantInt>(builder.build_value(term->alignment)))
              alignment = alignment_expr->getValue().getZExtValue();
          }

          PSI_ASSERT(dest->getType() == llvm::IntegerType::getInt8PtrTy(builder.module_builder()->llvm_context()));
          
          llvm::ConstantInt *alignment_expr = llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(builder.module_builder()->llvm_context()), alignment);
          llvm::Value *val = llvm::ConstantInt::get(llvm::IntegerType::getInt8Ty(builder.module_builder()->llvm_context()), 0);
          llvm::Value *isvolatile = llvm::ConstantInt::getFalse(builder.module_builder()->llvm_context());
          
          return builder.irbuilder().CreateCall5(builder.module_builder()->llvm_memset(), dest, val, count, alignment_expr, isvolatile);
        }
        
        typedef TermOperationMap<Instruction, llvm::Value*, FunctionBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<Return>(return_callback)
            .add<ConditionalBranch>(conditional_branch_callback)
            .add<UnconditionalBranch>(unconditional_branch_callback)
            .add<Unreachable>(unreachable_callback)
            .add<Call>(function_call_callback)
            .add<Load>(load_callback)
            .add<Store>(store_callback)
            .add<Alloca>(alloca_callback)
            .add<StackSave>(stack_save_callback)
            .add<StackRestore>(stack_restore_callback)
            .add<Evaluate>(evaluate_callback)
            .add<MemCpy>(memcpy_callback)
            .add<MemZero>(memzero_callback);
        }
      };

      InstructionBuilder::CallbackMap InstructionBuilder::callback_map(InstructionBuilder::callback_map_initializer());

      /**
       * Build a value for an instruction operation.
       *
       * This handles complex operations on aggregate types; numeric
       * operations are forwarded to build_value_instruction_simple.
       */
      llvm::Value* FunctionBuilder::build_value_instruction(const ValuePtr<Instruction>& term) {
        return InstructionBuilder::callback_map.call(*this, term);
      }
    }
  }
}
