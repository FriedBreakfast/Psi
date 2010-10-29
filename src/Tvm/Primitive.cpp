#include "Primitive.hpp"
#include "Functional.hpp"

#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    TermPtr<> Metatype::type(Context& context, std::size_t n_parameters, Term *const*) const {
      if (n_parameters != 0)
	throw std::logic_error("metatype created with parameters");
      return TermPtr<>();
    }

    LLVMValue Metatype::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return llvm_value_constant(builder, term);
    }

    LLVMValue Metatype::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      throw std::logic_error("metatype does not have a value");
    }

    LLVMType Metatype::llvm_type(LLVMValueBuilder& builder, Term&) const {
      return llvm_type(builder);
    }

    bool Metatype::operator == (const Metatype&) const {
      return true;
    }

    std::size_t hash_value(const Metatype&) {
      return 0;
    }

    /**
     * \brief Get the LLVM type for Metatype values.
     */
    LLVMType Metatype::llvm_type(LLVMValueBuilder& builder) {
      const llvm::Type* i64 = llvm::Type::getInt64Ty(builder.context());
      return LLVMType::known(llvm::StructType::get(builder.context(), i64, i64, NULL));
    }

    LLVMValue Metatype::llvm_value(LLVMValueBuilder& builder, std::size_t size, std::size_t align) {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      return llvm_from_constant(builder,
				llvm::ConstantInt::get(i64, size),
				llvm::ConstantInt::get(i64, align));
    }

    /**
     * \brief Get an LLVM value for Metatype for an empty type.
     */
    LLVMValue Metatype::llvm_empty(LLVMValueBuilder& builder) {
      return llvm_value(builder, 0, 1);
    }

    /**
     * \brief Get an LLVM value for Metatype for the given LLVM type.
     */
    LLVMValue Metatype::llvm_from_type(LLVMValueBuilder& builder, const llvm::Type* ty) {
      llvm::Constant* values[2] = {
	llvm::ConstantExpr::getSizeOf(ty),
	llvm::ConstantExpr::getAlignOf(ty)
      };

      return LLVMValue::known(llvm::ConstantStruct::get(builder.context(), values, 2, false));
    }

    /**
     * \brief Get an LLVM value for a specified size and alignment.
     *
     * The result of this call will be a global constant.
     */
    LLVMValue Metatype::llvm_from_constant(LLVMValueBuilder& builder, llvm::Constant *size, llvm::Constant *align) {
      if (!size->getType()->isIntegerTy(64) || !align->getType()->isIntegerTy(64))
	throw std::logic_error("size or align in metatype is not a 64-bit integer");

      if (llvm::cast<llvm::ConstantInt>(align)->getValue().exactLogBase2() < 0)
	throw std::logic_error("alignment is not a power of two");

      llvm::Constant* values[2] = {size, align};
      return LLVMValue::known(llvm::ConstantStruct::get(builder.context(), values, 2, false));
    }

    /**
     * \brief Get an LLVM value for a specified size and alignment.
     *
     * The result of this call will be a global constant.
     */
    LLVMValue Metatype::llvm_runtime(LLVMFunctionBuilder& builder, llvm::Value *size, llvm::Value *align) {
      const llvm::Type* i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Type *mtype = llvm::StructType::get(builder.context(), i64, i64, NULL);
      llvm::Value *first = builder.irbuilder().CreateInsertValue(llvm::UndefValue::get(mtype), size, 0);
      llvm::Value *second = builder.irbuilder().CreateInsertValue(first, align, 1);
      return LLVMValue::known(second);
    }

    FunctionalTermPtr<Metatype> Context::get_metatype() {
      return get_functional_v(Metatype());
    }

    LLVMType BlockType::llvm_type(LLVMValueBuilder& builder, Term&) const {
      return LLVMType::known(const_cast<llvm::Type*>(llvm::Type::getLabelTy(builder.context())));
    }

    bool BlockType::operator == (const BlockType&) const {
      return true;
    }

    std::size_t hash_value(const BlockType&) {
      return 0;
    }

    FunctionalTermPtr<BlockType> Context::get_block_type() {
      return get_functional_v(BlockType());
    }
  }
}
