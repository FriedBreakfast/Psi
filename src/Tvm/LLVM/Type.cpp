#include "Builder.hpp"

#include "../TermOperationMap.hpp"

#include "../Aggregate.hpp"
#include "../Number.hpp"
#include <../lib/llvm-2.8/include/llvm/Target/TargetData.h>

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct TypeBuilder {
        static const llvm::Type *metatype_callback(ConstantBuilder& builder, Metatype::Ptr) {
          const llvm::Type *intptr_ty = builder.llvm_target_machine()->getTargetData()->getIntPtrType(builder.llvm_context());
          std::vector<const llvm::Type*> elements(2, intptr_ty);
          return llvm::StructType::get(builder.llvm_context(), elements);
        }
        
        static const llvm::Type* empty_type_callback(ConstantBuilder& builder, EmptyType::Ptr) {
          return llvm::StructType::get(builder.llvm_context());
        }
        
        static const llvm::Type* pointer_type_callback(ConstantBuilder& builder, PointerType::Ptr) {
          return llvm::Type::getInt8PtrTy(builder.llvm_context());
        }
        
        static const llvm::Type* block_type_callback(ConstantBuilder& builder, BlockType::Ptr) {
          return llvm::Type::getLabelTy(builder.llvm_context());
        }
        
        static const llvm::Type* byte_type_callback(ConstantBuilder& builder, ByteType::Ptr) {
          return llvm::Type::getInt8Ty(builder.llvm_context());
        }

        static const llvm::Type* boolean_type_callback(ConstantBuilder& builder, BooleanType::Ptr) {
          return llvm::Type::getInt1Ty(builder.llvm_context());
        }

        static const llvm::Type* integer_type_callback(ConstantBuilder& builder, IntegerType::Ptr term) {
          return integer_type(builder.llvm_context(), builder.llvm_target_machine()->getTargetData(), term->width());
        }

        static const llvm::Type* float_type_callback(ConstantBuilder& builder, FloatType::Ptr term) {
          return float_type(builder.llvm_context(), term->width());
        }

        static const llvm::Type* array_type_callback(ConstantBuilder& builder, ArrayType::Ptr term) {
          const llvm::Type* element_type = builder.build_type(term->element_type());
          const llvm::APInt& length_value = builder.build_constant_integer(term->length());
          return llvm::ArrayType::get(element_type, length_value.getZExtValue());
        }

        static const llvm::Type* struct_type_callback(ConstantBuilder& builder, StructType::Ptr term) {
          std::vector<const llvm::Type*> member_types;
          for (unsigned i = 0, e = term->n_members(); i != e; ++i)
            member_types.push_back(builder.build_type(term->member_type(i)));

          return llvm::StructType::get(builder.llvm_context(), member_types);
        }
        
        typedef TermOperationMap<FunctionalTerm, const llvm::Type*, ConstantBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap::Initializer callback_map_initializer() {
          return CallbackMap::initializer()
            .add<Metatype>(metatype_callback)
            .add<EmptyType>(empty_type_callback)
            .add<PointerType>(pointer_type_callback)
            .add<BlockType>(block_type_callback)
            .add<ByteType>(byte_type_callback)
            .add<BooleanType>(boolean_type_callback)
            .add<IntegerType>(integer_type_callback)
            .add<FloatType>(float_type_callback)
            .add<ArrayType>(array_type_callback)
            .add<StructType>(struct_type_callback);
        }
      };
      
      TypeBuilder::CallbackMap TypeBuilder::callback_map(TypeBuilder::callback_map_initializer());

      /**
       * Internal function to do the actual work of building a
       * type. This function handles aggregate types, primitive types
       * are forwarded to build_type_internal_simple.
       */
      const llvm::Type* ConstantBuilder::build_type_internal(FunctionalTerm *term) {
        return TypeBuilder::callback_map.call(*this, term);
      }
    }
  }
}