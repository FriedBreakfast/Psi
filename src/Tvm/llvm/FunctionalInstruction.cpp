#include "Builder.hpp"

#include "../TermOperationMap.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionalInstructionBuilder {
        static llvm::Value *metatype_size_callback(FunctionBuilder& builder, const ValuePtr<MetatypeSize>& term) {
          llvm::Value *value = builder.build_value(term->parameter());
          return builder.irbuilder().CreateExtractValue(value, 0);
        }

        static llvm::Value *metatype_alignment_callback(FunctionBuilder& builder, const ValuePtr<MetatypeAlignment>& term) {
          llvm::Value *value = builder.build_value(term->parameter());
          return builder.irbuilder().CreateExtractValue(value, 1);
        }

        static llvm::Value* array_value_callback(FunctionBuilder& builder, const ValuePtr<ArrayValue>& term) {
          llvm::Type *type = builder.module_builder()->build_type(term->type());
          llvm::Value *array = llvm::UndefValue::get(type);
          for (unsigned i = 0; i < term->length(); ++i) {
            llvm::Value *element = builder.build_value(term->value(i));
            array = builder.irbuilder().CreateInsertValue(array, element, i);
          }

          return array;
        }

        static llvm::Value* struct_value_callback(FunctionBuilder& builder, const ValuePtr<StructValue>& term) {
          llvm::Type *type = builder.module_builder()->build_type(term->type());
          llvm::Value *result = llvm::UndefValue::get(type);
          for (std::size_t i = 0; i < term->n_members(); ++i) {
            llvm::Value *val = builder.build_value(term->member_value(i));
            result = builder.irbuilder().CreateInsertValue(result, val, i);
          }
          return result;
        }

        static llvm::Value* function_specialize_callback(FunctionBuilder& builder, const ValuePtr<FunctionSpecialize>& term) {
          return builder.build_value(term->function());
        }
        
        static llvm::Value* pointer_cast_callback(FunctionBuilder& builder, const ValuePtr<PointerCast>& term) {
          llvm::Type *type = builder.module_builder()->build_type(term->target_type());
          llvm::Value *source = builder.build_value(term->pointer());
          return builder.irbuilder().CreateBitCast(source, type->getPointerTo());
        }

        static llvm::Value* pointer_offset_callback(FunctionBuilder& builder, const ValuePtr<PointerOffset>& term) {
          llvm::Value *ptr = builder.build_value(term->pointer());
          llvm::Value *offset = builder.build_value(term->offset());
          return builder.irbuilder().CreateInBoundsGEP(ptr, offset);
        }
        
        static llvm::Value* struct_element_callback(FunctionBuilder& builder, const ValuePtr<StructElement>& term) {
          llvm::Value *aggregate = builder.build_value(term->aggregate());
          unsigned index = term->index();
          return builder.irbuilder().CreateExtractValue(aggregate, index);
        }
        
        static llvm::Value* element_ptr_callback(FunctionBuilder& builder, const ValuePtr<ElementPtr>& term) {
          llvm::Value *aggregate_ptr = builder.build_value(term->pointer());
          if (ValuePtr<StructMember> struct_m = dyn_cast<StructMember>(term->member())) {
            PSI_ASSERT(llvm::isa<llvm::StructType>(aggregate_ptr->getType()));
            return builder.irbuilder().CreateStructGEP(aggregate_ptr, struct_m->index());
          } else if (ValuePtr<ArrayMember> array_m = dyn_cast<ArrayMember>(term->member())) {
            PSI_ASSERT(llvm::isa<llvm::ArrayType>(aggregate_ptr->getType()));
            llvm::Type *i32_ty = llvm::Type::getInt32Ty(builder.module_builder()->llvm_context());
            llvm::Value *indices[2] = {llvm::ConstantInt::get(i32_ty, 0), builder.build_value(array_m->index())};
            return builder.irbuilder().CreateInBoundsGEP(aggregate_ptr, indices);
          } else {
            PSI_FAIL("unreachable");
          }
        }
        
        static llvm::Value* select_value_callback(FunctionBuilder& builder, const ValuePtr<Select>& term) {
          llvm::Value *condition = builder.build_value(term->condition());
          llvm::Value *true_value = builder.build_value(term->true_value());
          llvm::Value *false_value = builder.build_value(term->false_value());
          return builder.irbuilder().CreateSelect(condition, true_value, false_value);
        }
        
        struct UnaryOpHandler {
          typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,const llvm::Twine&);
          CallbackType callback;

          UnaryOpHandler(CallbackType callback_) : callback(callback_) {}

          llvm::Value* operator () (FunctionBuilder& builder, const ValuePtr<UnaryOp>& term) const {
            llvm::Value* parameter = builder.build_value(term->parameter());
            return (builder.irbuilder().*callback)(parameter, "");
          }
        };

        struct BinaryOpHandler {
          typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
          CallbackType callback;

          BinaryOpHandler(CallbackType callback_) : callback(callback_) {}

          llvm::Value* operator () (FunctionBuilder& builder, const ValuePtr<BinaryOp>& term) const {
            llvm::Value* lhs = builder.build_value(term->lhs());
            llvm::Value* rhs = builder.build_value(term->rhs());
            return (builder.irbuilder().*callback)(lhs, rhs, "");
          }
        };

        struct IntegerBinaryOpHandler {
          typedef llvm::Value* (IRBuilder::*CallbackType) (llvm::Value*,llvm::Value*,const llvm::Twine&);
          CallbackType ui_callback, si_callback;

          IntegerBinaryOpHandler(CallbackType ui_callback_, CallbackType si_callback_)
            : ui_callback(ui_callback_), si_callback(si_callback_) {
          }

          llvm::Value* operator () (FunctionBuilder& builder, const ValuePtr<BinaryOp>& term) const {
            llvm::Value* lhs = builder.build_value(term->lhs());
            llvm::Value* rhs = builder.build_value(term->rhs());
            if (value_cast<IntegerType>(term->lhs()->type())->is_signed())
              return (builder.irbuilder().*si_callback)(lhs, rhs, "");
            else
              return (builder.irbuilder().*ui_callback)(lhs, rhs, "");
          }
        };

        static llvm::Value *integer_divide_callback(FunctionBuilder& builder, const ValuePtr<IntegerDivide>& term) {
            llvm::Value* lhs = builder.build_value(term->lhs());
            llvm::Value* rhs = builder.build_value(term->rhs());
            if (value_cast<IntegerType>(term->type())->is_signed())
              return builder.irbuilder().CreateSDiv(lhs, rhs, "");
            else
              return builder.irbuilder().CreateUDiv(lhs, rhs, "");
        }
        
        typedef TermOperationMap<FunctionalValue, llvm::Value*, FunctionBuilder&> CallbackMap;
        
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
            .add<ElementPtr>(element_ptr_callback)
            .add<Select>(select_value_callback)
            .add<IntegerAdd>(IntegerBinaryOpHandler(&IRBuilder::CreateNUWAdd, &IRBuilder::CreateNSWAdd))
            .add<IntegerMultiply>(IntegerBinaryOpHandler(&IRBuilder::CreateNUWMul, &IRBuilder::CreateNSWMul))
            .add<IntegerDivide>(integer_divide_callback)
            .add<IntegerNegative>(UnaryOpHandler(&IRBuilder::CreateNSWNeg))
            .add<BitAnd>(BinaryOpHandler(&IRBuilder::CreateAnd))
            .add<BitOr>(BinaryOpHandler(&IRBuilder::CreateOr))
            .add<BitXor>(BinaryOpHandler(&IRBuilder::CreateXor))
            .add<BitNot>(UnaryOpHandler(&IRBuilder::CreateNot))
            .add<IntegerCompareEq>(BinaryOpHandler(&IRBuilder::CreateICmpEQ))
            .add<IntegerCompareNe>(BinaryOpHandler(&IRBuilder::CreateICmpNE))
            .add<IntegerCompareGt>(IntegerBinaryOpHandler(&IRBuilder::CreateICmpUGT, &IRBuilder::CreateICmpSGT))
            .add<IntegerCompareLt>(IntegerBinaryOpHandler(&IRBuilder::CreateICmpULT, &IRBuilder::CreateICmpSLT))
            .add<IntegerCompareGe>(IntegerBinaryOpHandler(&IRBuilder::CreateICmpUGE, &IRBuilder::CreateICmpSGE))
            .add<IntegerCompareLe>(IntegerBinaryOpHandler(&IRBuilder::CreateICmpULE, &IRBuilder::CreateICmpSLE));
        }
      };

      FunctionalInstructionBuilder::CallbackMap FunctionalInstructionBuilder::callback_map(FunctionalInstructionBuilder::callback_map_initializer());

      /**
       * Build a value for a functional operation.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_value_functional_simple.
       */
      llvm::Value* FunctionBuilder::build_value_functional(const ValuePtr<FunctionalValue>& term) {
        return FunctionalInstructionBuilder::callback_map.call(*this, term);
      }
    }
  }
}
