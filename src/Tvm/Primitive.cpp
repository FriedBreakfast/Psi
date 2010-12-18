#include "Primitive.hpp"
#include "Functional.hpp"

#include <llvm/LLVMContext.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    bool StatelessTerm::operator == (const StatelessTerm&) const {
      return true;
    }

    std::size_t hash_value(const StatelessTerm&) {
      return 0;
    }

    void PrimitiveTerm::check_primitive_parameters(ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 0)
        throw TvmUserError("primitive term created with parameters");
    }

    LLVMValue PrimitiveTerm::llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const {
      PSI_FAIL("llvm_value_instruction should never be called on primitive values");
    }

    FunctionalTypeResult PrimitiveType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      check_primitive_parameters(parameters);
      return FunctionalTypeResult(context.get_metatype(), false);
    }

    llvm::Constant* PrimitiveType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      return LLVMMetatype::from_type(builder, llvm_type(builder, term));
    }

    const llvm::Type* PrimitiveType::llvm_type(LLVMConstantBuilder& builder, FunctionalTerm&) const {
      return llvm_primitive_type(builder);
    }

    llvm::Type* ValueTerm::llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const {
      PSI_FAIL("the type of a term cannot a value term");
    }

    llvm::Constant* PrimitiveValue::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm&) const {
      return llvm_primitive_value(builder);
    }

    FunctionalTypeResult Metatype::type(Context&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 0)
	throw TvmUserError("metatype created with parameters");
      return FunctionalTypeResult(NULL, false);
    }

    llvm::Constant* Metatype::llvm_value_constant(LLVMConstantBuilder& builder, Term&) const {
      return LLVMMetatype::from_type(builder, LLVMMetatype::type(builder));
    }

    const llvm::Type* Metatype::llvm_type(LLVMConstantBuilder& builder, Term&) const {
      return LLVMMetatype::type(builder);
    }

    FunctionalTermPtr<Metatype> Context::get_metatype() {
      return get_functional_v(Metatype());
    }

    /**
     * Get a (or rather the) value of the empty type.
     */
    llvm::Constant* EmptyType::llvm_empty_value(LLVMConstantBuilder& c) {
      return llvm::ConstantStruct::get(c.llvm_context(), NULL, 0, false);
    }

    const llvm::Type* EmptyType::llvm_primitive_type(LLVMConstantBuilder& c) const {
      return llvm::StructType::get(c.llvm_context());
    }

    FunctionalTermPtr<EmptyType> Context::get_empty_type() {
      return get_functional_v(EmptyType());
    }

    const llvm::Type* BlockType::llvm_primitive_type(LLVMConstantBuilder& c) const {
      return llvm::Type::getLabelTy(c.llvm_context());
    }

    FunctionalTermPtr<BlockType> Context::get_block_type() {
      return get_functional_v(BlockType());
    }
  }
}
