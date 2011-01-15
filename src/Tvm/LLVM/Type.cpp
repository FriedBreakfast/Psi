#include "Builder.hpp"

#include "../TermOperationMap.hpp"

#include "../Aggregate.hpp"
#include "../Number.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      struct TypeBuilder {
        static const llvm::Type *metatype_type(ConstantBuilder& builder, Metatype::Ptr) {
          std::vector<const llvm::Type*> elements(2, builder.get_intptr_type());
          return llvm::StructType::get(builder.llvm_context(), elements);
        }
        
        static const llvm::Type* empty_type(ConstantBuilder& builder, EmptyType::Ptr) {
          return llvm::StructType::get(builder.llvm_context());
        }
        
        static const llvm::Type* pointer_type(ConstantBuilder& builder, PointerType::Ptr) {
          return builder.get_pointer_type();
        }
        
        static const llvm::Type* block_type(ConstantBuilder& builder, BlockType::Ptr) {
          return llvm::Type::getLabelTy(builder.llvm_context());
        }
        
        static const llvm::Type* byte_type(ConstantBuilder& builder, ByteType::Ptr) {
          return builder.get_byte_type();
        }

        static const llvm::Type* boolean_type(ConstantBuilder& builder, BooleanType::Ptr) {
          return builder.get_boolean_type();
        }

        static const llvm::Type* integer_type(ConstantBuilder& builder, IntegerType::Ptr term) {
          return builder.get_integer_type(term->width());
        }

        static const llvm::Type* float_type(ConstantBuilder& builder, FloatType::Ptr term) {
          return builder.get_float_type(term->width());
        }

        static const llvm::Type* array_type(ConstantBuilder& builder, ArrayType::Ptr term) {
          const llvm::Type* element_type = builder.build_type(term->element_type());
          const llvm::APInt& length_value = builder.build_constant_integer(term->length());
          return llvm::ArrayType::get(element_type, length_value.getZExtValue());
        }

        static const llvm::Type* struct_type(ConstantBuilder& builder, StructType::Ptr term) {
          std::vector<const llvm::Type*> member_types;
          for (unsigned i = 0, e = term->n_members(); i != e; ++i)
            member_types.push_back(builder.build_type(term->member_type(i)));

          return llvm::StructType::get(builder.llvm_context(), member_types);
        }
        
        typedef TermOperationMap<FunctionalTerm, const llvm::Type*, ConstantBuilder&> CallbackMap;
        
        static CallbackMap callback_map;
        
        static CallbackMap callback_map_initializer() {
          return CallbackMap::initializer()
            .add<Metatype>(metatype_type)
            .add<EmptyType>(empty_type)
            .add<PointerType>(pointer_type)
            .add<BlockType>(block_type)
            .add<ByteType>(byte_type)
            .add<BooleanType>(boolean_type)
            .add<IntegerType>(integer_type)
            .add<FloatType>(float_type)
            .add<ArrayType>(array_type)
            .add<StructType>(struct_type);
        }
      };
    }
  }
}