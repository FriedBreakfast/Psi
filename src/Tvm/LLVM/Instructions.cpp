#include "Builder.hpp"

#include "../Aggregate.hpp"
#include "../Instructions.hpp"
#include "../TermOperationMap.hpp"

#include <boost/assign.hpp>
#include <boost/make_shared.hpp>

#include <llvm/Function.h>
#include <llvm/Target/TargetData.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct InstructionBuilder {
        static llvm::Instruction* return_callback(FunctionBuilder& builder, Return::Ptr insn) {
          return builder.irbuilder().CreateRet(builder.build_value(insn->value()));
        }

        static llvm::Instruction* conditional_branch_callback(FunctionBuilder& builder, ConditionalBranch::Ptr insn) {
          llvm::Value *cond = builder.build_value(insn->condition());
          llvm::BasicBlock *true_target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->true_target()));
          llvm::BasicBlock *false_target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->false_target()));
          return builder.irbuilder().CreateCondBr(cond, true_target, false_target);
        }

        static llvm::Instruction* unconditional_branch_callback(FunctionBuilder& builder, UnconditionalBranch::Ptr insn) {
          llvm::BasicBlock *target = llvm::cast<llvm::BasicBlock>(builder.build_value(insn->target()));
          return builder.irbuilder().CreateBr(target);
        }

        static llvm::Instruction* function_call_callback(FunctionBuilder& builder, FunctionCall::Ptr insn) {
          FunctionTypeTerm* function_type = cast<FunctionTypeTerm>
            (cast<PointerType>(insn->target()->type())->target_type());
            
          const llvm::Type *llvm_function_type = builder.build_type(function_type)->getPointerTo();

          llvm::Value *target = builder.build_value(insn->target());

          std::size_t n_phantom = function_type->n_phantom_parameters();
          std::size_t n_passed_parameters = function_type->n_parameters() - n_phantom;

          llvm::SmallVector<llvm::Value*, 4> parameters(n_passed_parameters);
          for (std::size_t i = 0; i < n_passed_parameters; ++i)
            parameters[i] = builder.build_value(insn->parameter(i + n_phantom));
          
          llvm::Value *cast_target = builder.irbuilder().CreatePointerCast(target, llvm_function_type);
          return builder.irbuilder().CreateCall(cast_target, parameters.begin(), parameters.end());
        }

        static llvm::Instruction* load_callback(FunctionBuilder& builder, Load::Ptr term) {
          llvm::Value *target = builder.build_value(term->target());
          return builder.irbuilder().CreateLoad(target);
        }

        static llvm::Instruction* store_callback(FunctionBuilder& builder, Store::Ptr term) {
          llvm::Value *target = builder.build_value(term->target());
          llvm::Value *value = builder.build_value(term->value());
          return builder.irbuilder().CreateStore(value, target);
        }

        static llvm::Instruction* alloca_callback(FunctionBuilder& builder, Alloca::Ptr term) {
          const llvm::Type *stored_type = builder.build_type(term->stored_type());
          llvm::Value *count = builder.build_value(term->count());
          llvm::Value *alignment = builder.build_value(term->alignment());
          llvm::AllocaInst *inst = builder.irbuilder().CreateAlloca(stored_type, count);
          
          if (llvm::ConstantInt *const_alignment = llvm::dyn_cast<llvm::ConstantInt>(alignment)) {
            inst->setAlignment(const_alignment->getValue().getZExtValue());
          } else {
            inst->setAlignment(builder.unknown_alloca_align());
          }
          
          return inst;
        }
        
        static llvm::Instruction* memcpy_callback(FunctionBuilder& builder, MemCpy::Ptr term) {
          llvm::Value *dest = builder.build_value(term->dest());
          llvm::Value *src = builder.build_value(term->src());
          llvm::Value *count = builder.build_value(term->count());
          unsigned alignment = 0;
          if (llvm::ConstantInt *alignment_expr = llvm::dyn_cast<llvm::ConstantInt>(builder.build_value(term->alignment())))
            alignment = alignment_expr->getValue().getZExtValue();
          
          PSI_ASSERT(dest->getType() == src->getType());

          const llvm::Type *i8ptr = llvm::IntegerType::getInt8PtrTy(builder.llvm_context());
          if (dest->getType() != i8ptr) {
            const llvm::TargetData *target_data = builder.llvm_target_machine()->getTargetData();
            const llvm::Type *element_type = llvm::cast<llvm::PointerType>(dest->getType())->getElementType();
            llvm::Constant *target_size = llvm::ConstantInt::get(target_data->getIntPtrType(builder.llvm_context()), target_data->getTypeAllocSize(element_type));
            count = builder.irbuilder().CreateMul(count, target_size);
            alignment = std::max(alignment, target_data->getABITypeAlignment(element_type));
            
            dest = builder.irbuilder().CreateBitCast(dest, i8ptr);
            src = builder.irbuilder().CreateBitCast(src, i8ptr);
          }
          
          llvm::ConstantInt *alignment_expr = llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(builder.llvm_context()), alignment);
          llvm::Value *isvolatile = llvm::ConstantInt::getFalse(builder.llvm_context());
          
          return builder.irbuilder().CreateCall5(builder.llvm_memcpy(), dest, src, count, alignment_expr, isvolatile);
        }
        
        static llvm::Value* eager_callback(FunctionBuilder& builder, Eager::Ptr term) {
          return builder.build_value(term->value());
        }

        typedef TermOperationMap<InstructionTerm, llvm::Value*, FunctionBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<Return>(return_callback)
            .add<ConditionalBranch>(conditional_branch_callback)
            .add<UnconditionalBranch>(unconditional_branch_callback)
            .add<FunctionCall>(function_call_callback)
            .add<Load>(load_callback)
            .add<Store>(store_callback)
            .add<Alloca>(alloca_callback)
            .add<MemCpy>(memcpy_callback)
            .add<Eager>(eager_callback);
        }
      };

      InstructionBuilder::CallbackMap InstructionBuilder::callback_map(InstructionBuilder::callback_map_initializer());

      /**
       * Build a value for an instruction operation.
       *
       * This handles complex operations on aggregate types; numeric
       * operations are forwarded to build_value_instruction_simple.
       */
      llvm::Value* FunctionBuilder::build_value_instruction(InstructionTerm *term) {
        return InstructionBuilder::callback_map.call(*this, term);
      }
    }
  }
}
