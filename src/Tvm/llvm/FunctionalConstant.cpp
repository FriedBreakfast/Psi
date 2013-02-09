#include "Builder.hpp"

#include "../TermOperationMap.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionalConstantBuilder {
        static llvm::Constant *metatype_size_callback(ModuleBuilder& builder, const ValuePtr<MetatypeSize>& term) {
          llvm::Type *type = builder.build_type(term->parameter());
          uint64_t size = builder.llvm_target_machine()->getDataLayout()->getTypeAllocSize(type);
          return llvm::ConstantInt::get(builder.llvm_target_machine()->getDataLayout()->getIntPtrType(builder.llvm_context()), size);
        }

        static llvm::Constant *metatype_alignment_callback(ModuleBuilder& builder, const ValuePtr<MetatypeAlignment>& term) {
          llvm::Type *type = builder.build_type(term->parameter());
          uint64_t size = builder.llvm_target_machine()->getDataLayout()->getABITypeAlignment(type);
          return llvm::ConstantInt::get(builder.llvm_target_machine()->getDataLayout()->getIntPtrType(builder.llvm_context()), size);
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
          llvm::IntegerType *llvm_type = integer_type(builder.llvm_context(), builder.llvm_target_machine()->getDataLayout(), term->type()->width());
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
          const llvm::StructLayout *layout = builder.llvm_target_machine()->getDataLayout()->getStructLayout(struct_type);
          uint64_t value = layout->getElementOffset(term->index());
          llvm::Type *size_type = builder.llvm_target_machine()->getDataLayout()->getIntPtrType(builder.llvm_context());
          return llvm::ConstantInt::get(size_type, value);
        }
        
        static llvm::Constant* select_value_callback(ModuleBuilder& builder, const ValuePtr<Select>& term) {
          llvm::Constant *condition = builder.build_constant(term->condition());
          llvm::Constant *true_value = builder.build_constant(term->true_value());
          llvm::Constant *false_value = builder.build_constant(term->false_value());
          return llvm::ConstantExpr::getSelect(condition, true_value, false_value);
        }
        
        static llvm::Constant* zext_or_trunc(llvm::Constant *val, llvm::Type *ty) {
          unsigned val_bits = val->getType()->getScalarSizeInBits(), ty_bits = ty->getScalarSizeInBits();
          if (val_bits < ty_bits)
            return llvm::ConstantExpr::getZExt(val, ty);
          else if (val_bits > ty_bits)
            return llvm::ConstantExpr::getTrunc(val, ty);
          else {
            PSI_ASSERT(val->getType() == ty);
            return val;
          }
        }

        static llvm::Constant* bitcast_callback(ModuleBuilder& builder, const ValuePtr<BitCast>& term) {
          llvm::Constant *value = builder.build_constant(term->value());
          llvm::Type *target_type = builder.build_type(term->target_type());
          
          if (target_type->isPointerTy() && value->getType()->isPointerTy())
            return llvm::ConstantExpr::getPointerCast(value, target_type);
          else if (!target_type->isPointerTy() && !value->getType()->isPointerTy() && (target_type->getPrimitiveSizeInBits() == value->getType()->getPrimitiveSizeInBits()))
            return llvm::ConstantExpr::getBitCast(value, target_type);
          
          llvm::Constant *value_int;
          if (value->getType()->isIntegerTy()) {
            value_int = value;
          } else if (value->getType()->isPointerTy()) {
            llvm::Type *value_int_type = llvm::Type::getIntNTy(builder.llvm_context(), target_type->getPrimitiveSizeInBits());
            value_int = llvm::ConstantExpr::getPtrToInt(value, value_int_type);
          } else {
            llvm::Type *value_int_type = llvm::Type::getIntNTy(builder.llvm_context(), value->getType()->getPrimitiveSizeInBits());
            value_int = llvm::ConstantExpr::getBitCast(value, value_int_type);
          }
          
          if (target_type->isPointerTy())
            return llvm::ConstantExpr::getIntToPtr(value_int, target_type);
          
          llvm::Type *target_int_type;
          if (target_type->isIntegerTy())
            target_int_type = target_type;
          else
            target_int_type = llvm::Type::getIntNTy(builder.llvm_context(), target_type->getPrimitiveSizeInBits());
          
          llvm::Constant *target_sized_value = zext_or_trunc(value_int, target_int_type);
          
          if (target_sized_value->getType() == target_type)
            return target_sized_value;
          else
            return llvm::ConstantExpr::getBitCast(target_sized_value, target_type);
        }
        
        static llvm::Constant* shl_callback(ModuleBuilder& builder, const ValuePtr<ShiftLeft>& term) {
          llvm::Constant *value = builder.build_constant(term->lhs()), *shift = builder.build_constant(term->rhs());
          shift = zext_or_trunc(shift, value->getType());
          return llvm::ConstantExpr::getShl(value, shift);
        }
        
        static llvm::Constant* shr_callback(ModuleBuilder& builder, const ValuePtr<ShiftRight>& term) {
          llvm::Constant *value = builder.build_constant(term->lhs()), *shift = builder.build_constant(term->rhs());
          shift = zext_or_trunc(shift, value->getType());
          return value_cast<IntegerType>(term->type())->is_signed() ?
            llvm::ConstantExpr::getAShr(value, shift) : llvm::ConstantExpr::getLShr(value, shift);
        }
        
        static llvm::Constant* add_callback(ModuleBuilder& builder, const ValuePtr<IntegerAdd>& term) {
          llvm::Constant *lhs = builder.build_constant(term->lhs()), *rhs = builder.build_constant(term->rhs());
          return llvm::ConstantExpr::getAdd(lhs, rhs);
        }
        
        static llvm::Constant* mul_callback(ModuleBuilder& builder, const ValuePtr<IntegerMultiply>& term) {
          llvm::Constant *lhs = builder.build_constant(term->lhs()), *rhs = builder.build_constant(term->rhs());
          return llvm::ConstantExpr::getMul(lhs, rhs);
        }

        static llvm::Constant* div_callback(ModuleBuilder& builder, const ValuePtr<IntegerDivide>& term) {
          llvm::Constant *lhs = builder.build_constant(term->lhs()), *rhs = builder.build_constant(term->rhs());
          if (value_cast<IntegerType>(term->type())->is_signed())
            return llvm::ConstantExpr::getSDiv(lhs, rhs);
          else
            return llvm::ConstantExpr::getUDiv(lhs, rhs);
        }
        
        static llvm::Constant* neg_callback(ModuleBuilder& builder, const ValuePtr<IntegerNegative>& term) {
          return llvm::ConstantExpr::getNeg(builder.build_constant(term->parameter()));
        }
        
        static llvm::Constant* not_callback(ModuleBuilder& builder, const ValuePtr<BitNot>& term) {
          return llvm::ConstantExpr::getNot(builder.build_constant(term->parameter()));
        }

        struct IntegerBinaryOpHandler {
          typedef llvm::Constant* (*CallbackType) (llvm::Constant*, llvm::Constant*);
          CallbackType callback;

          IntegerBinaryOpHandler(CallbackType callback_) : callback(callback_) {}

          llvm::Constant* operator () (ModuleBuilder& builder, const ValuePtr<IntegerBinaryOp>& term) const {
            llvm::Constant *lhs = builder.build_constant(term->lhs());
            llvm::Constant *rhs = builder.build_constant(term->rhs());
            return callback(lhs, rhs);
          }
        };
        
        struct IntegerCompareHandler {
          unsigned short ui_predicate, si_predicate;
          
          IntegerCompareHandler(unsigned short predicate)
            : ui_predicate(predicate), si_predicate(predicate) {}

          IntegerCompareHandler(unsigned short ui_predicate_, unsigned short si_predicate_)
            : ui_predicate(ui_predicate_), si_predicate(si_predicate_) {}

          llvm::Constant* operator () (ModuleBuilder& builder, const ValuePtr<IntegerCompareOp>& term) const {
            llvm::Constant *lhs = builder.build_constant(term->lhs());
            llvm::Constant *rhs = builder.build_constant(term->rhs());
            return llvm::ConstantExpr::getICmp(value_cast<IntegerType>(term->lhs()->type())->is_signed() ? si_predicate : ui_predicate, lhs, rhs);
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
            .add<BitCast>(bitcast_callback)
            .add<ShiftLeft>(shl_callback)
            .add<ShiftRight>(shr_callback)
            .add<IntegerAdd>(add_callback)
            .add<IntegerMultiply>(mul_callback)
            .add<IntegerDivide>(div_callback)
            .add<IntegerNegative>(neg_callback)
            .add<BitAnd>(IntegerBinaryOpHandler(llvm::ConstantExpr::getAnd))
            .add<BitOr>(IntegerBinaryOpHandler(llvm::ConstantExpr::getOr))
            .add<BitXor>(IntegerBinaryOpHandler(llvm::ConstantExpr::getXor))
            .add<BitNot>(not_callback)
            .add<IntegerCompareEq>(IntegerCompareHandler(llvm::CmpInst::ICMP_EQ))
            .add<IntegerCompareNe>(IntegerCompareHandler(llvm::CmpInst::ICMP_NE))
            .add<IntegerCompareGt>(IntegerCompareHandler(llvm::CmpInst::ICMP_UGT, llvm::CmpInst::ICMP_SGT))
            .add<IntegerCompareLt>(IntegerCompareHandler(llvm::CmpInst::ICMP_ULT, llvm::CmpInst::ICMP_SLT))
            .add<IntegerCompareGe>(IntegerCompareHandler(llvm::CmpInst::ICMP_UGE, llvm::CmpInst::ICMP_SGE))
            .add<IntegerCompareLe>(IntegerCompareHandler(llvm::CmpInst::ICMP_ULE, llvm::CmpInst::ICMP_SLE));
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
