#include "Builder.hpp"

#include "../TermOperationMap.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionalInstructionBuilder {
        static llvm::Value *metatype_size_callback(FunctionBuilder& builder, MetatypeSize::Ptr term) {
          llvm::Value *value = builder.build_value(term->parameter());
          return builder.irbuilder().CreateExtractValue(value, 0);
        }

        static llvm::Value *metatype_alignment_callback(FunctionBuilder& builder, MetatypeAlignment::Ptr term) {
          llvm::Value *value = builder.build_value(term->parameter());
          return builder.irbuilder().CreateExtractValue(value, 1);
        }

        static llvm::Value* array_value_callback(FunctionBuilder& builder, ArrayValue::Ptr term) {
          const llvm::Type *type = builder.build_type(term->type());
          llvm::Value *array = llvm::UndefValue::get(type);
          for (unsigned i = 0; i < term->length(); ++i) {
            llvm::Value *element = builder.build_value(term->value(i));
            array = builder.irbuilder().CreateInsertValue(array, element, i);
          }

          return array;
        }

        static llvm::Value* struct_value_callback(FunctionBuilder& builder, StructValue::Ptr term) {
          const llvm::Type *type = builder.build_type(term->type());
          llvm::Value *result = llvm::UndefValue::get(type);
          for (std::size_t i = 0; i < term->n_members(); ++i) {
            llvm::Value *val = builder.build_value(term->member_value(i));
            result = builder.irbuilder().CreateInsertValue(result, val, i);
          }
          return result;
        }

        static llvm::Value* function_specialize_callback(FunctionBuilder& builder, FunctionSpecialize::Ptr term) {
          return builder.build_value(term->function());
        }
        
        static llvm::Value* pointer_cast_callback(FunctionBuilder& builder, PointerCast::Ptr term) {
          const llvm::Type *type = builder.build_type(term->target_type());
          llvm::Value *source = builder.build_value(term->pointer());
          return builder.irbuilder().CreateBitCast(source, type->getPointerTo());
        }

        static llvm::Value* pointer_offset_callback(FunctionBuilder& builder, PointerOffset::Ptr term) {
          llvm::Value *ptr = builder.build_value(term->pointer());
          llvm::Value *offset = builder.build_value(term->offset());
          return builder.irbuilder().CreateInBoundsGEP(ptr, offset);
        }
        
        static llvm::Value* struct_element_callback(FunctionBuilder& builder, StructElement::Ptr term) {
          llvm::Value *aggregate = builder.build_value(term->aggregate());
          unsigned index = term->index();
          return builder.irbuilder().CreateExtractValue(aggregate, index);
        }
        
        static llvm::Value* struct_element_ptr_callback(FunctionBuilder& builder, StructElementPtr::Ptr term) {
          llvm::Value *aggregate_ptr = builder.build_value(term->aggregate_ptr());
          return builder.irbuilder().CreateStructGEP(aggregate_ptr, term->index());
        }
        
        static llvm::Value* array_element_ptr_callback(FunctionBuilder& builder, ArrayElementPtr::Ptr term) {
          llvm::Value *aggregate_ptr = builder.build_value(term->aggregate_ptr());
          const llvm::Type *i32_ty = llvm::Type::getInt32Ty(builder.llvm_context());
          llvm::Value *indices[2] = {llvm::ConstantInt::get(i32_ty, 0), builder.build_value(term->index())};
          return builder.irbuilder().CreateInBoundsGEP(aggregate_ptr, indices, indices+2);
        }
        
        static llvm::Value* select_value_callback(FunctionBuilder& builder, SelectValue::Ptr term) {
          llvm::Value *condition = builder.build_value(term->condition());
          llvm::Value *true_value = builder.build_value(term->true_value());
          llvm::Value *false_value = builder.build_value(term->false_value());
          return builder.irbuilder().CreateSelect(condition, true_value, false_value);
        }
        
        struct UnaryOp {
          typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,const llvm::Twine&);
          CallbackType callback;

          UnaryOp(CallbackType callback_) : callback(callback_) {}

          llvm::Value* operator () (FunctionBuilder& builder, UnaryOperation::Ptr term) const {
            llvm::Value* parameter = builder.build_value(term->parameter());
            return (builder.irbuilder().*callback)(parameter, "");
          }
        };

        struct BinaryOp {
          typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
          CallbackType callback;

          BinaryOp(CallbackType callback_) : callback(callback_) {}

          llvm::Value* operator () (FunctionBuilder& builder, BinaryOperation::Ptr term) const {
            llvm::Value* lhs = builder.build_value(term->lhs());
            llvm::Value* rhs = builder.build_value(term->rhs());
            return (builder.irbuilder().*callback)(lhs, rhs, "");
          }
        };

        struct IntegerBinaryOp {
          typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
          CallbackType ui_callback, si_callback;

          IntegerBinaryOp(CallbackType ui_callback_, CallbackType si_callback_)
            : ui_callback(ui_callback_), si_callback(si_callback_) {
          }

          llvm::Value* operator () (FunctionBuilder& builder, BinaryOperation::Ptr term) const {
            llvm::Value* lhs = builder.build_value(term->lhs());
            llvm::Value* rhs = builder.build_value(term->rhs());
            if (cast<IntegerType>(term->lhs()->type())->is_signed())
              return (builder.irbuilder().*si_callback)(lhs, rhs, "");
            else
              return (builder.irbuilder().*ui_callback)(lhs, rhs, "");
          }
        };
        
        typedef TermOperationMap<FunctionalTerm, llvm::Value*, FunctionBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<MetatypeSize>(metatype_size_callback)
            .add<MetatypeAlignment>(metatype_alignment_callback)
            .add<ArrayValue>(array_value_callback)
            .add<StructValue>(struct_value_callback)
            .add<FunctionSpecialize>(function_specialize_callback)
            .add<PointerCast>(pointer_cast_callback)
            .add<PointerOffset>(pointer_offset_callback)
            .add<StructElement>(struct_element_callback)
            .add<StructElementPtr>(struct_element_ptr_callback)
            .add<ArrayElementPtr>(array_element_ptr_callback)
            .add<SelectValue>(select_value_callback)
            .add<IntegerAdd>(BinaryOp(&IRBuilder::CreateAdd))
            .add<IntegerMultiply>(BinaryOp(&IRBuilder::CreateMul))
            .add<IntegerDivide>(IntegerBinaryOp(&IRBuilder::CreateUDiv, &IRBuilder::CreateSDiv))
            .add<IntegerNegative>(UnaryOp(&IRBuilder::CreateNeg))
            .add<BitAnd>(BinaryOp(&IRBuilder::CreateAnd))
            .add<BitOr>(BinaryOp(&IRBuilder::CreateOr))
            .add<BitXor>(BinaryOp(&IRBuilder::CreateXor))
            .add<BitNot>(UnaryOp(&IRBuilder::CreateNot))
            .add<IntegerCompareEq>(BinaryOp(&IRBuilder::CreateICmpEQ))
            .add<IntegerCompareNe>(BinaryOp(&IRBuilder::CreateICmpNE))
            .add<IntegerCompareGt>(IntegerBinaryOp(&IRBuilder::CreateICmpUGT, &IRBuilder::CreateICmpSGT))
            .add<IntegerCompareLt>(IntegerBinaryOp(&IRBuilder::CreateICmpULT, &IRBuilder::CreateICmpSLT))
            .add<IntegerCompareGe>(IntegerBinaryOp(&IRBuilder::CreateICmpUGE, &IRBuilder::CreateICmpSGE))
            .add<IntegerCompareLe>(IntegerBinaryOp(&IRBuilder::CreateICmpULE, &IRBuilder::CreateICmpSLE));
        }
      };

      FunctionalInstructionBuilder::CallbackMap FunctionalInstructionBuilder::callback_map(FunctionalInstructionBuilder::callback_map_initializer());

      /**
       * Build a value for a functional operation.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_value_functional_simple.
       */
      llvm::Value* FunctionBuilder::build_value_functional(FunctionalTerm *term) {
        return FunctionalInstructionBuilder::callback_map.call(*this, term);
      }
    }
  }
}
