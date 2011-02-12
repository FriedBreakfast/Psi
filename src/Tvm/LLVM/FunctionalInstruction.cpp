#include "Builder.hpp"

#include "../TermOperationMap.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct FunctionalInstructionBuilder {
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
          return builder.build_value(term->pointer());
        }

        static llvm::Value *metatype_size_callback(FunctionBuilder& builder, MetatypeSize::Ptr term) {
          llvm::Value *value = builder.build_value(term->parameter());
          return builder.irbuilder().CreateExtractValue(value, 0);
        }

        static llvm::Value *metatype_alignment_callback(FunctionBuilder& builder, MetatypeAlignment::Ptr term) {
          llvm::Value *value = builder.build_value(term->parameter());
          return builder.irbuilder().CreateExtractValue(value, 1);
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
            if (cast<IntegerType>(term->type())->is_signed())
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
            .add<IntegerAdd>(BinaryOp(&IRBuilder::CreateAdd))
            .add<IntegerMultiply>(BinaryOp(&IRBuilder::CreateMul))
            .add<IntegerDivide>(IntegerBinaryOp(&IRBuilder::CreateUDiv, &IRBuilder::CreateSDiv))
            .add<IntegerNegative>(UnaryOp(&IRBuilder::CreateNeg));
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
