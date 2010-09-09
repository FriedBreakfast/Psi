#include "Derived.hpp"

#include <stdexcept>

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    namespace {
      std::pair<llvm::Constant*, llvm::Constant*> constant_size_align(llvm::Constant *value) {
	unsigned zero = 0, one = 1;
	return std::make_pair(llvm::ConstantExpr::getExtractValue(value, &zero, 1),
			      llvm::ConstantExpr::getExtractValue(value, &one, 1));
      }

      llvm::Constant* constant_max(llvm::Constant* left, llvm::Constant* right) {
	llvm::Constant* cmp = llvm::ConstantExpr::getCompare(llvm::CmpInst::ICMP_ULT, left, right);
	return llvm::ConstantExpr::getSelect(cmp, left, right);
      }

      /*
       * Align a size to a boundary. The formula is: <tt>(size + align
       * - 1) & ~align</tt>. <tt>align</tt> must be a power of two.
       */
      llvm::Constant* constant_align(llvm::Constant* size, llvm::Constant* align) {
	llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
	llvm::Constant* a = llvm::ConstantExpr::getSub(align, one);
	llvm::Constant* b = llvm::ConstantExpr::getAdd(size, a);
	llvm::Constant* c = llvm::ConstantExpr::getNot(align);
	return llvm::ConstantExpr::getAnd(b, c);
      }

      std::pair<llvm::Value*, llvm::Value*> instruction_size_align(LLVMFunctionBuilder::IRBuilder& irbuilder, llvm::Value *value) {
	llvm::Value *size = irbuilder.CreateExtractValue(value, 0);
	llvm::Value *align = irbuilder.CreateExtractValue(value, 1);
	return std::make_pair(size, align);
      }

      llvm::Value* instruction_max(LLVMFunctionBuilder::IRBuilder& irbuilder, llvm::Value *left, llvm::Value *right) {
	llvm::Value *cmp = irbuilder.CreateICmpULT(left, right);
	return irbuilder.CreateSelect(cmp, left, right);
      }

      /* See constant_align */
      llvm::Value* instruction_align(LLVMFunctionBuilder::IRBuilder& irbuilder, llvm::Value* size, llvm::Value* align) {
	llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
	llvm::Value* a = irbuilder.CreateSub(align, one);
	llvm::Value* b = irbuilder.CreateAdd(size, a);
	llvm::Value* c = irbuilder.CreateNot(align);
	return irbuilder.CreateAnd(b, c);
      }
    }

    DerivedType::DerivedType() : Type(term_functional) {
    }

    bool DerivedType::equals_internal(const ProtoTerm&) const {
      return true;
    }

    std::size_t DerivedType::hash_internal() const {
      return 0;
    }

    Term* PointerType::create(Term *target) {
      return target->context().new_term(PointerType(), target);
    }

    ProtoTerm* PointerType::clone() const {
      return new PointerType(*this);
    }

    LLVMFunctionBuilder::Result PointerType::llvm_value_instruction(LLVMFunctionBuilder& builder, Term*) const {
      return Metatype::llvm_value(llvm::Type::getInt8PtrTy(builder.context()));
    }

    LLVMConstantBuilder::Constant PointerType::llvm_value_constant(LLVMConstantBuilder& builder, Term*) const {
      return Metatype::llvm_value(llvm::Type::getInt8PtrTy(builder.context()));
    }

    LLVMConstantBuilder::Type PointerType::llvm_type(LLVMConstantBuilder& builder, Term*) const {
      return LLVMConstantBuilder::type_known(const_cast<llvm::PointerType*>(llvm::Type::getInt8PtrTy(builder.context())));
    }

    Term* ArrayType::create(Context& context, Term *element_type, Term *size) {
      if (element_type->proto().category() != term_type)
	throw std::logic_error("array element type is not a type");
      return context.new_term(ArrayType(), element_type, size);
    }

    ProtoTerm* ArrayType::clone() const {
      return new ArrayType(*this);
    }

    LLVMFunctionBuilder::Result ArrayType::llvm_value_instruction(LLVMFunctionBuilder& builder, Term* term) const {
      Term *element_type = term->parameter(0);
      Term *length = term->parameter(1);
      
      LLVMFunctionBuilder::Result element_size_align = builder.value(element_type);
      LLVMFunctionBuilder::Result length_value = builder.value(length);

      if (!length_value.known())
	throw std::logic_error("array length value is not a known constant");

      if (element_size_align.empty()) {
	return Metatype::llvm_value_empty(builder.context());
      } else {
	std::pair<llvm::Value*,llvm::Value*> size_align =
	  instruction_size_align(builder.irbuilder(), element_size_align.value());
	llvm::Value *size = builder.irbuilder().CreateMul(size_align.first, length_value.value());
	return Metatype::llvm_value(builder, size, size_align.second);
      }
    }

    LLVMConstantBuilder::Constant ArrayType::llvm_value_constant(LLVMConstantBuilder& builder, Term* term) const {
      Term *element_type = term->parameter(0);
      Term *length = term->parameter(1);
      
      LLVMConstantBuilder::Constant element_size_align = builder.constant(element_type);
      LLVMConstantBuilder::Constant length_value = builder.constant(length);

      if (!length_value.known())
	throw std::logic_error("constant array length value is not a known constant");

      if (element_size_align.empty()) {
	return Metatype::llvm_value_empty(builder.context());
      } else {
	std::pair<llvm::Constant*,llvm::Constant*> size_align =
	  constant_size_align(element_size_align.value());
	llvm::Constant *size = llvm::ConstantExpr::getMul(size_align.first, length_value.value());
	return Metatype::llvm_value(size, size_align.second);
      }
    }

    LLVMConstantBuilder::Type ArrayType::llvm_type(LLVMConstantBuilder& builder, Term* term) const {
      Term *element_type = term->parameter(0);
      Term *length = term->parameter(1);

      LLVMConstantBuilder::Type element_llvm_type = builder.type(element_type);
      LLVMConstantBuilder::Constant length_value = builder.constant(length);

      if (!length_value.known() || !element_llvm_type.known())
	return LLVMConstantBuilder::type_unknown();

      llvm::ConstantInt *length_llvm = llvm::cast<llvm::ConstantInt>(length_value.value());
      if (!length_llvm)
	return LLVMConstantBuilder::type_unknown();

      return LLVMConstantBuilder::type_known(llvm::ArrayType::get(element_llvm_type.type(), length_llvm->getZExtValue()));
    }

    LLVMConstantBuilder::Constant StructType::llvm_value_constant(LLVMConstantBuilder& builder, Term* term) const {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_size_align(builder.constant(param).value());
	size = llvm::ConstantExpr::getAdd(constant_align(size, size_align.second), size);
	align = constant_max(align, size_align.second);
      }

      // size should always be a multiple of align
      size = constant_align(size, align);

      return Metatype::llvm_value(size, align);
    }

    LLVMFunctionBuilder::Result StructType::llvm_value_instruction(LLVMFunctionBuilder& builder, Term *term) const {
      LLVMFunctionBuilder::IRBuilder& irbuilder = builder.irbuilder();
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Value *size = llvm::ConstantInt::get(i64, 0);
      llvm::Value *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMFunctionBuilder::Result param_result = builder.value(param);
	PSI_ASSERT(param_result.known(), "Value of metatype is not known");
	std::pair<llvm::Value*, llvm::Value*> size_align = instruction_size_align(irbuilder, param_result.value());
	size = irbuilder.CreateAdd(instruction_align(irbuilder, size, size_align.second), size);
	align = instruction_max(irbuilder, align, size_align.second);
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);

      return Metatype::llvm_value(builder, size, align);
    }

    LLVMConstantBuilder::Type StructType::llvm_type(LLVMConstantBuilder& builder, Term *term) const {
      std::vector<const llvm::Type*> member_types;
      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMConstantBuilder::Type param_result = builder.type(param);
	if (param_result.known()) {
	  member_types.push_back(param_result.type());
	} else if (!param_result.empty()) {
	  return LLVMConstantBuilder::type_unknown();
	}
      }

      if (!member_types.empty()) {
	return LLVMConstantBuilder::type_known(llvm::StructType::get(builder.context(), member_types));
      } else {
	return LLVMConstantBuilder::type_empty();
      }
    }

    LLVMFunctionBuilder::Result UnionType::llvm_value_instruction(LLVMFunctionBuilder& builder, Term* term) const {
      LLVMFunctionBuilder::IRBuilder& irbuilder = builder.irbuilder();
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Value *size = llvm::ConstantInt::get(i64, 0);
      llvm::Value *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMFunctionBuilder::Result param_result = builder.value(param);
	PSI_ASSERT(param_result.known(), "Value of metatype is not known");
	std::pair<llvm::Value*, llvm::Value*> size_align = instruction_size_align(irbuilder, param_result.value());	
	size = instruction_max(irbuilder, size, size_align.first);
	align = instruction_max(irbuilder, size, size_align.second);
      }

      // size must be a multiple of align
      size = instruction_align(irbuilder, size, align);

      return Metatype::llvm_value(builder, size, align);
    }

    LLVMConstantBuilder::Constant UnionType::llvm_value_constant(LLVMConstantBuilder& builder, Term* term) const {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMConstantBuilder::Constant param_result = builder.constant(param);
	PSI_ASSERT(param_result.known(), "Value of metatype is not known");
	std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_size_align(param_result.value());	
	size = constant_max(size, size_align.first);
	align = constant_max(size, size_align.second);
      }

      // size must be a multiple of align
      size = constant_align(size, align);

      return Metatype::llvm_value(size, align);
    }

    LLVMConstantBuilder::Type UnionType::llvm_type(LLVMConstantBuilder& builder, Term* term) const {
      std::vector<const llvm::Type*> lm;
      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMConstantBuilder::Type param_result = builder.type(param);
	if (param_result.known()) {
	  lm.push_back(param_result.type());
	} else if (!param_result.empty()) {
	  return LLVMConstantBuilder::type_unknown();
	}
      }

      if (!lm.empty())
	return LLVMConstantBuilder::type_known(llvm::UnionType::get(&lm[0], lm.size()));
      else
	return LLVMConstantBuilder::type_empty();
    }
  }
}
