#include "Builder.hpp"

#include "../TermOperationMap.hpp"

#include "../Aggregate.hpp"
#include "../Number.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct TypeBuilder {
        static llvm::Type *metatype_callback(ModuleBuilder& builder, const ValuePtr<Metatype>&) {
          llvm::Type *intptr_ty = builder.llvm_target_machine()->getDataLayout()->getIntPtrType(builder.llvm_context());
          llvm::Type *elements[] = {intptr_ty, intptr_ty};
          return llvm::StructType::get(builder.llvm_context(), elements, false);
        }
        
        static llvm::Type* empty_type_callback(ModuleBuilder& builder, const ValuePtr<EmptyType>&) {
          return llvm::StructType::get(builder.llvm_context());
        }
        
        static llvm::Type* pointer_type_callback(ModuleBuilder& builder, const ValuePtr<PointerType>& term) {
          return builder.build_type(term->target_type())->getPointerTo();
        }
        
        static llvm::Type* block_type_callback(ModuleBuilder& builder, const ValuePtr<BlockType>&) {
          return llvm::Type::getLabelTy(builder.llvm_context());
        }
        
        static llvm::Type* byte_type_callback(ModuleBuilder& builder, const ValuePtr<ByteType>&) {
          return llvm::Type::getInt8Ty(builder.llvm_context());
        }
        
        static llvm::Type* boolean_type_callback(ModuleBuilder& builder, const ValuePtr<BooleanType>&) {
          return llvm::Type::getInt1Ty(builder.llvm_context());
        }

        static llvm::Type* integer_type_callback(ModuleBuilder& builder, const ValuePtr<IntegerType>& term) {
          return integer_type(builder.llvm_context(), builder.llvm_target_machine()->getDataLayout(), term->width());
        }

        static llvm::Type* float_type_callback(ModuleBuilder& builder, const ValuePtr<FloatType>& term) {
          return float_type(builder.llvm_context(), term->width());
        }

        static llvm::Type* array_type_callback(ModuleBuilder& builder, const ValuePtr<ArrayType>& term) {
          llvm::Type* element_type = builder.build_type(term->element_type());
          const llvm::APInt& length_value = builder.build_constant_integer(term->length());
          return llvm::ArrayType::get(element_type, length_value.getZExtValue());
        }

        static llvm::Type* struct_type_callback(ModuleBuilder& builder, const ValuePtr<StructType>& term) {
          llvm::SmallVector<llvm::Type*, 8> member_types;
          for (unsigned i = 0, e = term->n_members(); i != e; ++i)
            member_types.push_back(builder.build_type(term->member_type(i)));

          return llvm::StructType::get(builder.llvm_context(), member_types);
        }
        
        typedef TermOperationMap<FunctionalValue, llvm::Type*, ModuleBuilder&> CallbackMap;
        
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
      llvm::Type* ModuleBuilder::build_type_internal(const ValuePtr<FunctionalValue>& term) {
        return TypeBuilder::callback_map.call(*this, term);
      }
    }
  }
}