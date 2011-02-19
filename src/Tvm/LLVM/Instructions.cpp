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

        typedef TermOperationMap<InstructionTerm, llvm::Instruction*, FunctionBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<Return>(return_callback)
            .add<ConditionalBranch>(conditional_branch_callback)
            .add<UnconditionalBranch>(unconditional_branch_callback)
            .add<FunctionCall>(function_call_callback)
            .add<Load>(load_callback)
            .add<Store>(store_callback)
            .add<Alloca>(alloca_callback);
        }
      };

      InstructionBuilder::CallbackMap InstructionBuilder::callback_map(InstructionBuilder::callback_map_initializer());

      /**
       * Build a value for an instruction operation.
       *
       * This handles complex operations on aggregate types; numeric
       * operations are forwarded to build_value_instruction_simple.
       */
      llvm::Instruction* FunctionBuilder::build_value_instruction(InstructionTerm *term) {
        return InstructionBuilder::callback_map.call(*this, term);
      }
    }
  }
}
