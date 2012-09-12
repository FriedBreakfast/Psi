#include "Builder.hpp"

#include "../TermOperationMap.hpp"

#include <llvm/Target/TargetData.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionalConstantBuilder {
        static llvm::Constant *metatype_size_callback(ModuleBuilder& builder, const ValuePtr<MetatypeSize>& term) {
          llvm::Type *type = builder.build_type(term->parameter());
          uint64_t size = builder.llvm_target_machine()->getTargetData()->getTypeAllocSize(type);
          return llvm::ConstantInt::get(builder.llvm_target_machine()->getTargetData()->getIntPtrType(builder.llvm_context()), size);
        }

        static llvm::Constant *metatype_alignment_callback(ModuleBuilder& builder, const ValuePtr<MetatypeAlignment>& term) {
          llvm::Type *type = builder.build_type(term->parameter());
          uint64_t size = builder.llvm_target_machine()->getTargetData()->getABITypeAlignment(type);
          return llvm::ConstantInt::get(builder.llvm_target_machine()->getTargetData()->getIntPtrType(builder.llvm_context()), size);
        }

        static llvm::Constant* empty_value_callback(ModuleBuilder& builder, const ValuePtr<EmptyValue>&) {
          return llvm::ConstantStruct::get(llvm::StructType::get(builder.llvm_context()), llvm::ArrayRef<llvm::Constant*>());
        }

        static llvm::Constant* boolean_value_callback(ModuleBuilder& builder, const ValuePtr<BooleanValue>& term) {
          return term->value() ? 
            llvm::ConstantInt::getTrue(builder.llvm_context())
            : llvm::ConstantInt::getFalse(builder.llvm_context());
        }

        static llvm::Constant* integer_value_callback(ModuleBuilder& builder, const ValuePtr<IntegerValue>& term) {
          llvm::IntegerType *llvm_type = integer_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData(), term->type()->width());
          llvm::APInt llvm_value(llvm_type->getBitWidth(), term->value().num_words(), term->value().words());
          return llvm::ConstantInt::get(llvm_type, llvm_value);
        }

        static llvm::Constant* float_value_callback(ModuleBuilder& builder, const ValuePtr<FloatValue>& term) {
          PSI_NOT_IMPLEMENTED();
        }

        static llvm::Constant* array_value_callback(ModuleBuilder& builder, const ValuePtr<ArrayValue>& term) {
          llvm::Type *type = builder.build_type(term->type());
          llvm::SmallVector<llvm::Constant*, 4> elements(term->length());
          for (unsigned i = 0; i < term->length(); ++i)
            elements[i] = builder.build_constant(term->value(i));

          return llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(type), elements);
        }

        static llvm::Constant* struct_value_callback(ModuleBuilder& builder, const ValuePtr<StructValue>& term) {
          llvm::StructType *type = llvm::cast<llvm::StructType>(builder.build_type(term->type()));
          llvm::SmallVector<llvm::Constant*, 4> members(term->n_members());
          for (unsigned i = 0; i < term->n_members(); ++i)
            members[i] = builder.build_constant(term->member_value(i));

          return llvm::ConstantStruct::get(type, members);
        }
        
        static llvm::Constant* undefined_value_callback(ModuleBuilder& builder, const ValuePtr<UndefinedValue>& term) {
          llvm::Type *ty = builder.build_type(term->type());
          return llvm::UndefValue::get(ty);
        }

        static llvm::Constant* function_specialize_callback(ModuleBuilder& builder, const ValuePtr<FunctionSpecialize>& term) {
          return builder.build_constant(term->function());
        }

        static llvm::Constant* pointer_cast_callback(ModuleBuilder& builder, const ValuePtr<PointerCast>& term) {
          llvm::Type *type = builder.build_type(term->target_type());
          llvm::Constant *source = builder.build_constant(term->pointer());
          return llvm::ConstantExpr::getBitCast(source, type->getPointerTo());
        }
        
        static llvm::Constant* pointer_offset_callback(ModuleBuilder& builder, const ValuePtr<PointerOffset>& term) {
          llvm::Constant *ptr = builder.build_constant(term->pointer());
          llvm::Constant *offset[] = {builder.build_constant(term->offset())};
          return llvm::ConstantExpr::getInBoundsGetElementPtr(ptr, offset);
        }
        
        static llvm::Constant* element_value_callback(ModuleBuilder& builder, const ValuePtr<ElementValue>& term) {
          llvm::Constant *aggregate = builder.build_constant(term->aggregate());
          unsigned indices[] = {builder.build_constant_integer(term->index()).getZExtValue()};
          return llvm::ConstantExpr::getExtractValue(aggregate, indices);
        }
        
        static llvm::Constant* element_ptr_callback(ModuleBuilder& builder, const ValuePtr<ElementPtr>& term) {
          llvm::Constant *aggregate_ptr = builder.build_constant(term->aggregate_ptr());
          llvm::Type *i32_ty = llvm::Type::getInt32Ty(builder.llvm_context());

          // Need to ensure the index is i32 for a struct because this is required by LLVM
          llvm::Constant *idx;
          if (llvm::isa<llvm::StructType>(llvm::cast<llvm::PointerType>(aggregate_ptr->getType())->getElementType()))
            idx = llvm::ConstantInt::get(i32_ty, builder.build_constant_integer(term->index()));
          else
            idx = builder.build_constant(term->index());

          llvm::Constant *indices[] = {llvm::ConstantInt::get(i32_ty, 0), idx};
          return llvm::ConstantExpr::getGetElementPtr(aggregate_ptr, indices);
        }
        
        static llvm::Constant* struct_element_offset_callback(ModuleBuilder& builder, const ValuePtr<StructElementOffset>& term) {
          llvm::StructType *struct_type = llvm::cast<llvm::StructType>(builder.build_type(term->struct_type()));
          const llvm::StructLayout *layout = builder.llvm_target_machine()->getTargetData()->getStructLayout(struct_type);
          uint64_t value = layout->getElementOffset(term->index());
          llvm::Type *size_type = builder.llvm_target_machine()->getTargetData()->getIntPtrType(builder.llvm_context());
          return llvm::ConstantInt::get(size_type, value);
        }
        
        static llvm::Constant* select_value_callback(ModuleBuilder& builder, const ValuePtr<Select>& term) {
          llvm::Constant *condition = builder.build_constant(term->condition());
          llvm::Constant *true_value = builder.build_constant(term->true_value());
          llvm::Constant *false_value = builder.build_constant(term->false_value());
          return llvm::ConstantExpr::getSelect(condition, true_value, false_value);
        }
        
        struct IntegerUnaryOpHandler {
          typedef llvm::APInt (llvm::APInt::*CallbackType) () const;
          CallbackType callback;
          
          IntegerUnaryOpHandler(CallbackType callback_) : callback(callback_) {}
          
          llvm::Constant* operator () (ModuleBuilder& builder, const ValuePtr<IntegerUnaryOp>& term) const {
            llvm::IntegerType *llvm_type = integer_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData(), value_cast<IntegerType>(term->type())->width());
            llvm::APInt param = builder.build_constant_integer(term->parameter());
            llvm::APInt result = (param.*callback)();
            return llvm::ConstantInt::get(llvm_type, result);
          }
        };

        struct IntegerBinaryOpHandler {
          typedef llvm::APInt (llvm::APInt::*CallbackType) (const llvm::APInt&) const;
          CallbackType ui_callback, si_callback;

          IntegerBinaryOpHandler(CallbackType callback_)
            : ui_callback(callback_), si_callback(callback_) {}

          IntegerBinaryOpHandler(CallbackType ui_callback_, CallbackType si_callback_)
            : ui_callback(ui_callback_), si_callback(si_callback_) {}

          llvm::Constant* operator () (ModuleBuilder& builder, const ValuePtr<IntegerBinaryOp>& term) const {
            llvm::IntegerType *llvm_type = integer_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData(), value_cast<IntegerType>(term->type())->width());
            llvm::APInt lhs = builder.build_constant_integer(term->lhs());
            llvm::APInt rhs = builder.build_constant_integer(term->rhs());
            llvm::APInt result;
            if (value_cast<IntegerType>(term->type())->is_signed())
              result = (lhs.*si_callback)(rhs);
            else
              result = (lhs.*ui_callback)(rhs);
            return llvm::ConstantInt::get(llvm_type, result);
          }
        };
        
        struct IntegerCompareOpHandler {
          typedef bool (llvm::APInt::*CallbackType) (const llvm::APInt&) const;
          CallbackType ui_callback, si_callback;

          IntegerCompareOpHandler(CallbackType callback_)
            : ui_callback(callback_), si_callback(callback_) {}

          IntegerCompareOpHandler(CallbackType ui_callback_, CallbackType si_callback_)
            : ui_callback(ui_callback_), si_callback(si_callback_) {}

          llvm::Constant* operator () (ModuleBuilder& builder, const ValuePtr<IntegerCompareOp>& term) const {
            llvm::APInt lhs = builder.build_constant_integer(term->lhs());
            llvm::APInt rhs = builder.build_constant_integer(term->rhs());
            bool pred_passed;
            if (value_cast<IntegerType>(term->lhs()->type())->is_signed())
              pred_passed = (lhs.*si_callback)(rhs);
            else
              pred_passed = (lhs.*ui_callback)(rhs);
            return pred_passed ? llvm::ConstantInt::getTrue(builder.llvm_context()) : llvm::ConstantInt::getFalse(builder.llvm_context());
          }
        };

        typedef TermOperationMap<FunctionalValue, llvm::Constant*, ModuleBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<MetatypeSize>(metatype_size_callback)
            .add<MetatypeAlignment>(metatype_alignment_callback)
            .add<EmptyValue>(empty_value_callback)
            .add<BooleanValue>(boolean_value_callback)
            .add<IntegerValue>(integer_value_callback)
            .add<FloatValue>(float_value_callback)
            .add<ArrayValue>(array_value_callback)
            .add<StructValue>(struct_value_callback)
            .add<UndefinedValue>(undefined_value_callback)
            .add<FunctionSpecialize>(function_specialize_callback)
            .add<PointerCast>(pointer_cast_callback)
            .add<PointerOffset>(pointer_offset_callback)
            .add<ElementValue>(element_value_callback)
            .add<ElementPtr>(element_ptr_callback)
            .add<StructElementOffset>(struct_element_offset_callback)
            .add<Select>(select_value_callback)
            .add<IntegerAdd>(IntegerBinaryOpHandler(&llvm::APInt::operator +))
            .add<IntegerMultiply>(IntegerBinaryOpHandler(&llvm::APInt::operator *))
            .add<IntegerDivide>(IntegerBinaryOpHandler(&llvm::APInt::udiv, &llvm::APInt::sdiv))
            .add<IntegerNegative>(IntegerUnaryOpHandler(&llvm::APInt::operator -))
            .add<BitAnd>(IntegerBinaryOpHandler(&llvm::APInt::operator &))
            .add<BitOr>(IntegerBinaryOpHandler(&llvm::APInt::operator |))
            .add<BitXor>(IntegerBinaryOpHandler(&llvm::APInt::operator ^))
            .add<BitNot>(IntegerUnaryOpHandler(&llvm::APInt::operator ~))
            .add<IntegerCompareEq>(IntegerCompareOpHandler(&llvm::APInt::eq))
            .add<IntegerCompareNe>(IntegerCompareOpHandler(&llvm::APInt::ne))
            .add<IntegerCompareGt>(IntegerCompareOpHandler(&llvm::APInt::ugt, &llvm::APInt::sgt))
            .add<IntegerCompareLt>(IntegerCompareOpHandler(&llvm::APInt::ult, &llvm::APInt::slt))
            .add<IntegerCompareGe>(IntegerCompareOpHandler(&llvm::APInt::uge, &llvm::APInt::sge))
            .add<IntegerCompareLe>(IntegerCompareOpHandler(&llvm::APInt::ule, &llvm::APInt::sle));
        }
      };
      
      FunctionalConstantBuilder::CallbackMap FunctionalConstantBuilder::callback_map(FunctionalConstantBuilder::callback_map_initializer());

      /**
       * Build an LLVM constant. The second component of the return value is
       * the required alignment of the return value.
       *
       * This handles aggregate types. Primitive types are forwarded
       * to build_constant_internal_simple.
       */
      llvm::Constant* ModuleBuilder::build_constant_internal(const ValuePtr<FunctionalValue>& term) {
        return FunctionalConstantBuilder::callback_map.call(*this, term);
      }
    }
  }
}
