#include "Builder.hpp"

#include "../TermOperationMap.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionalConstantBuilder {
        static llvm::Constant *type_callback(ConstantBuilder& builder, Term *type) {
          return metatype_from_type(builder, builder.build_type(type));
        }
        
        static llvm::Constant *metatype_size_callback(ConstantBuilder& builder, MetatypeSize::Ptr term) {
          llvm::Constant *value = builder.build_constant(term->parameter());
          unsigned zero = 0;
          return llvm::ConstantExpr::getExtractValue(value, &zero, 1);
        }

        static llvm::Constant *metatype_alignment_callback(ConstantBuilder& builder, MetatypeAlignment::Ptr term) {
          llvm::Constant *value = builder.build_constant(term->parameter());
          unsigned one = 1;
          return llvm::ConstantExpr::getExtractValue(value, &one, 1);
        }

        static llvm::Constant* empty_value_callback(ConstantBuilder& builder, EmptyValue::Ptr) {
          return llvm::ConstantStruct::get(builder.llvm_context(), 0, 0, false);
        }

        static llvm::Constant* boolean_value_callback(ConstantBuilder& builder, BooleanValue::Ptr term) {
          return term->value() ? 
            llvm::ConstantInt::getTrue(builder.llvm_context())
            : llvm::ConstantInt::getFalse(builder.llvm_context());
        }

        static llvm::Constant* integer_value_callback(ConstantBuilder& builder, IntegerValue::Ptr term) {
          const llvm::IntegerType *llvm_type = integer_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData(), term->type()->width());
          llvm::APInt llvm_value(llvm_type->getBitWidth(), term->value().num_words(), term->value().words());
          return llvm::ConstantInt::get(llvm_type, llvm_value);
        }

        static llvm::Constant* float_value_callback(ConstantBuilder& builder, FloatValue::Ptr term) {
          PSI_FAIL("not implemented");
        }

        static llvm::Constant* array_value_callback(ConstantBuilder& builder, ArrayValue::Ptr term) {
          const llvm::Type *type = builder.build_type(term->type());
          llvm::SmallVector<llvm::Constant*, 4> elements(term->length());
          for (unsigned i = 0; i < term->length(); ++i)
            elements[i] = builder.build_constant(term->value(i));

          return llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(type), &elements[0], elements.size());
        }

        static llvm::Constant* struct_value_callback(ConstantBuilder& builder, StructValue::Ptr term) {
          llvm::SmallVector<llvm::Constant*, 4> members(term->n_members());
          for (unsigned i = 0; i < term->n_members(); ++i)
            members[i] = builder.build_constant(term->member_value(i));

          return llvm::ConstantStruct::get(builder.llvm_context(), &members[0], members.size(), false);
        }
        
        static llvm::Constant* undefined_value_callback(ConstantBuilder& builder, UndefinedValue::Ptr term) {
          const llvm::Type *ty = builder.build_type(term->type());
          return llvm::UndefValue::get(ty);
        }

        static llvm::Constant* function_specialize_callback(ConstantBuilder& builder, FunctionSpecialize::Ptr term) {
          return builder.build_constant(term->function());
        }

        static llvm::Constant* pointer_cast_callback(ConstantBuilder& builder, PointerCast::Ptr term) {
          return builder.build_constant(term->pointer());
        }

        struct IntegerBinaryOp {
          typedef llvm::APInt (llvm::APInt::*CallbackType) (const llvm::APInt&) const;
          CallbackType ui_callback, si_callback;

          IntegerBinaryOp(CallbackType callback_)
            : ui_callback(callback_), si_callback(callback_) {}

          IntegerBinaryOp(CallbackType ui_callback_, CallbackType si_callback_)
            : ui_callback(ui_callback_), si_callback(si_callback_) {}

          llvm::Constant* operator () (ConstantBuilder& builder, BinaryOperation::Ptr term) const {
            const llvm::IntegerType *llvm_type = integer_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData(), cast<IntegerType>(term->type())->width());
            llvm::APInt lhs = builder.build_constant_integer(term->lhs());
            llvm::APInt rhs = builder.build_constant_integer(term->rhs());
            llvm::APInt result;
            if (cast<IntegerType>(term->type())->is_signed())
              result = (lhs.*si_callback)(rhs);
            else
              result = (lhs.*ui_callback)(rhs);
            return llvm::ConstantInt::get(llvm_type, result);
          }
        };

        typedef TermOperationMap<FunctionalTerm, llvm::Constant*, ConstantBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<Metatype>(type_callback)
            .add<EmptyType>(type_callback)
            .add<PointerType>(type_callback)
            .add<BlockType>(type_callback)
            .add<ByteType>(type_callback)
            .add<BooleanType>(type_callback)
            .add<IntegerType>(type_callback)
            .add<FloatType>(type_callback)
            .add<ArrayType>(type_callback)
            .add<StructType>(type_callback)
            .add<MetatypeSize>(metatype_size_callback)
            .add<MetatypeAlignment>(metatype_alignment_callback)
            .add<EmptyValue>(empty_value_callback)
            .add<BooleanValue>(boolean_value_callback)
            .add<IntegerValue>(integer_value_callback)
            .add<FloatValue>(float_value_callback)
            .add<ArrayValue>(array_value_callback)
            .add<StructValue>(struct_value_callback)
            .add<UndefinedValue>(undefined_value_callback)
            .add<PointerCast>(pointer_cast_callback)
            .add<FunctionSpecialize>(function_specialize_callback)
            .add<IntegerAdd>(IntegerBinaryOp(&llvm::APInt::operator +))
            .add<IntegerMultiply>(IntegerBinaryOp(&llvm::APInt::operator *))
            .add<IntegerDivide>(IntegerBinaryOp(&llvm::APInt::udiv, &llvm::APInt::sdiv));
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
      llvm::Constant* ModuleBuilder::build_constant_internal(FunctionalTerm *term) {
        return FunctionalConstantBuilder::callback_map.call(*this, term);
      }
    }
  }
}
