#include "Derived.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"
#include "LLVMBuilder.hpp"
#include "Number.hpp"

#include <stdexcept>

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    FunctionalTypeResult PointerType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 1)
	throw TvmUserError("pointer type takes one parameter");
      if (!parameters[0]->is_type())
        throw TvmUserError("pointer argument must be a type");
      return FunctionalTypeResult(context.get_metatype(), false);
    }

    LLVMValue PointerType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm&) const {
      return LLVMValue::known(llvm_value(builder.llvm_context()));
    }

    llvm::Constant* PointerType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm&) const {
      return llvm_value(builder.llvm_context());
    }

    llvm::Constant* PointerType::llvm_value(llvm::LLVMContext& context) {
      return LLVMMetatype::from_type(llvm::Type::getInt8PtrTy(context));
    }

    const llvm::Type* PointerType::llvm_type(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);
      const llvm::Type *target_ty = builder.build_type(self.target_type());
      if (target_ty)
        return target_ty->getPointerTo();
      else
        return llvm::Type::getInt8PtrTy(builder.llvm_context());
    }

    FunctionalTermPtr<PointerType> Context::get_pointer_type(Term* type) {
      return get_functional_v(PointerType(), type);
    }

    namespace {
      llvm::Constant* type_size_align(LLVMConstantBuilder& builder, Term *type) {
        PSI_ASSERT(!type->phantom());
        if (const llvm::Type* llvm_type = builder.build_type(type)) {
          return LLVMMetatype::from_type(llvm_type);
        } else {
          return NULL;
        }
      }

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

      /**
       * Get the size and alignment of values of type \c type. Returns
       * boost::none if the type is empty.
       */
      std::pair<llvm::Constant*, llvm::Constant*> constant_type_size_align(LLVMConstantBuilder& builder, Term *type) {
        const llvm::Type* llvm_type = builder.build_type(type);
        if (llvm_type) {
          return std::make_pair(llvm::ConstantExpr::getSizeOf(llvm_type),
                                llvm::ConstantExpr::getAlignOf(llvm_type));
        } else {
          return constant_size_align(builder.build_constant(type));
        }
      }

      std::pair<llvm::Value*, llvm::Value*> instruction_size_align(LLVMIRBuilder& irbuilder, llvm::Value *value) {
	llvm::Value *size = irbuilder.CreateExtractValue(value, 0);
	llvm::Value *align = irbuilder.CreateExtractValue(value, 1);
	return std::make_pair(size, align);
      }

      llvm::Value* instruction_max(LLVMIRBuilder& irbuilder, llvm::Value *left, llvm::Value *right) {
	llvm::Value *cmp = irbuilder.CreateICmpULT(left, right);
	return irbuilder.CreateSelect(cmp, left, right);
      }

      /* See constant_align */
      llvm::Value* instruction_align(LLVMIRBuilder& irbuilder, llvm::Value* size, llvm::Value* align) {
	llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
	llvm::Value* a = irbuilder.CreateSub(align, one);
	llvm::Value* b = irbuilder.CreateAdd(size, a);
	llvm::Value* c = irbuilder.CreateNot(align);
	return irbuilder.CreateAnd(b, c);
      }

      /**
       * Get the size and alignment of values of type \c type. Returns
       * boost::none if the type is empty.
       */
      std::pair<llvm::Value*, llvm::Value*> instruction_type_size_align(LLVMFunctionBuilder& builder, Term *type) {
        const llvm::Type* llvm_type = builder.build_type(type);
        if (llvm_type) {
          return std::make_pair<llvm::Value*,llvm::Value*>
            (llvm::ConstantExpr::getSizeOf(llvm_type),
             llvm::ConstantExpr::getAlignOf(llvm_type));
        } else {
          llvm::Value *llvm_value = builder.build_known_value(type);
          return instruction_size_align(builder.irbuilder(), llvm_value);
        }
      }
    }

    FunctionalTypeResult ArrayType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 2)
	throw TvmUserError("array type term takes two parameters");

      if (!parameters[0]->is_type())
	throw TvmUserError("first argument to array type term is not a type");

      if (parameters[1]->type() != context.get_integer_type(64, false))
	throw TvmUserError("second argument to array type term is not a 64-bit integer");

      return FunctionalTypeResult(context.get_metatype(), parameters[0]->phantom() || parameters[1]->phantom());
    }

    LLVMValue ArrayType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return LLVMValue::known(type_value);

      std::pair<llvm::Value*,llvm::Value*> size_align = instruction_type_size_align(builder, self.element_type());
      llvm::Value *length = builder.build_known_value(self.length());
      llvm::Value *array_size = builder.irbuilder().CreateMul(size_align.first, length);
      return LLVMMetatype::from_value(builder.irbuilder(), array_size, size_align.second);
    }

    llvm::Constant* ArrayType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant* type_value = type_size_align(builder, &term))
        return type_value;

      std::pair<llvm::Constant*,llvm::Constant*> size_align = constant_type_size_align(builder, self.element_type());
      llvm::Constant *length = builder.build_constant(self.length());
      llvm::Constant *total_size = llvm::ConstantExpr::getMul(size_align.first, length);
      return LLVMMetatype::from_constant(total_size, size_align.second);
    }

    const llvm::Type* ArrayType::llvm_type(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      const llvm::Type* element_type = builder.build_type(self.element_type());
      if (!element_type)
        return NULL;

      llvm::ConstantInt *length_value = llvm::dyn_cast<llvm::ConstantInt>(builder.build_constant(self.length()));
      if (!length_value)
        return NULL;

      return llvm::ArrayType::get(element_type, length_value->getZExtValue());
    }

    FunctionalTermPtr<ArrayType> Context::get_array_type(Term* element_type, Term* length) {
      return get_functional_v(ArrayType(), element_type, length);
    }

    FunctionalTermPtr<ArrayType> Context::get_array_type(Term* element_type, std::size_t length) {
      Term* length_term = get_functional_v(ConstantInteger(IntegerType(false, 64), length));
      return get_functional_v(ArrayType(), element_type, length_term);
    }

    FunctionalTypeResult ArrayValue::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() < 1)
        throw TvmUserError("array values require at least one parameter");

      if (!parameters[0]->is_type())
        throw TvmUserError("first argument to array value is not a type");

      bool phantom = false;
      for (std::size_t i = 1; i < parameters.size(); ++i) {
        if (parameters[i]->type() != parameters[0])
          throw TvmUserError("array value element is of the wrong type");
        phantom = phantom || parameters[i]->phantom();
      }

      PSI_ASSERT(phantom || !parameters[0]->phantom());

      return FunctionalTypeResult(context.get_array_type(parameters[0], parameters.size() - 1), phantom);
    }

    LLVMValue ArrayValue::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (const llvm::Type *array_type = builder.build_type(term.type())) {
        llvm::Value *array = llvm::UndefValue::get(array_type);

        for (std::size_t i = 0; i < self.length(); ++i) {
          llvm::Value *element = builder.build_known_value(self.value(i));
          array = builder.irbuilder().CreateInsertValue(array, element, i);
        }

        return LLVMValue::known(array);
      } else {
        PSI_FAIL("not implemented");
      }
    }

    llvm::Constant* ArrayValue::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      const llvm::Type* array_type = builder.build_type(term.type());
      PSI_ASSERT(array_type);

      std::vector<llvm::Constant*> elements;
      for (std::size_t i = 0; i < self.length(); ++i)
        elements.push_back(builder.build_constant(self.value(i)));

      return llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(array_type), elements);
    }

    FunctionalTermPtr<ArrayValue> Context::get_array_value(Term* element_type, ArrayPtr<Term*const> elements) {
      ScopedTermPtrArray<> parameters(elements.size() + 1);
      parameters[0] = element_type;
      for (std::size_t i = 0; i < elements.size(); ++i)
        parameters[i+1] = elements[i];
      return get_functional(ArrayValue(), parameters.array());
    }

    FunctionalTypeResult AggregateType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      bool phantom = false;
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (!parameters[i]->is_type())
          throw TvmUserError("members of an aggregate type must be types");
        phantom = phantom || parameters[i]->phantom();
      }

      return FunctionalTypeResult(context.get_metatype(), phantom);
    }

    LLVMValue StructType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return LLVMValue::known(type_value);

      PSI_ASSERT(self.n_members() > 0);

      LLVMIRBuilder& irbuilder = builder.irbuilder();
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.llvm_context());
      llvm::Value *size = llvm::ConstantInt::get(i64, 0);
      llvm::Value *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        std::pair<llvm::Value*, llvm::Value*> size_align = instruction_type_size_align(builder, self.member_type(i));
        size = irbuilder.CreateAdd(instruction_align(irbuilder, size, size_align.second), size_align.first);
        align = instruction_max(irbuilder, align, size_align.second);
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);
      return LLVMMetatype::from_value(builder.irbuilder(), size, align);
    }

    llvm::Constant* StructType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return type_value;

      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.llvm_context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_type_size_align(builder, self.member_type(i));
        size = llvm::ConstantExpr::getAdd(constant_align(size, size_align.second), size_align.first);
        align = constant_max(align, size_align.second);
      }

      // size should always be a multiple of align
      size = constant_align(size, align);
      return LLVMMetatype::from_constant(size, align);
    }

    const llvm::Type* StructType::llvm_type(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      std::vector<const llvm::Type*> member_types;
      for (std::size_t i = 0; i < self.n_members(); ++i) {
	if (const llvm::Type* param_result = builder.build_type(self.member_type(i))) {
	  member_types.push_back(param_result);
	} else {
          return NULL;
	}
      }

      return llvm::StructType::get(builder.llvm_context(), member_types);
    }

    FunctionalTermPtr<StructType> Context::get_struct_type(ArrayPtr<Term*const> parameters) {
      return get_functional(StructType(), parameters);
    }

    FunctionalTypeResult StructValue::type(Context& context, ArrayPtr<Term*const> parameters) const {
      ScopedTermPtrArray<> member_types(parameters.size());

      bool phantom = false;
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        phantom = phantom || parameters[i]->phantom();
        member_types[i] = parameters[i]->type();
      }

      Term *type = context.get_struct_type(member_types.array());
      PSI_ASSERT(phantom == type->phantom());

      return FunctionalTypeResult(type, phantom);
    }

    LLVMValue StructValue::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      PSI_ASSERT(!term.phantom());

      if (const llvm::Type* ty = builder.build_type(term.type())) {
        llvm::Value *result = llvm::UndefValue::get(ty);
        for (std::size_t i = 0; i < self.n_members(); ++i) {
          llvm::Value *val = builder.build_known_value(self.member_value(i));
          result = builder.irbuilder().CreateInsertValue(result, val, i);
        }
        return LLVMValue::known(result);
      } else {
        llvm::Value *storage = builder.create_alloca_for(term.type());

        llvm::Value *offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.llvm_context()), 0);
        for (std::size_t i = 0; i < self.n_members(); ++i) {
          LLVMValue member_val = builder.build_value(self.member_value(i));
          if (member_val.is_known()) {
            llvm::Constant *size = llvm::ConstantExpr::getSizeOf(member_val.known_value()->getType());
            llvm::Constant *align = llvm::ConstantExpr::getAlignOf(member_val.known_value()->getType());
            // align pointer to boundary
            offset = instruction_align(builder.irbuilder(), offset, align);
            llvm::Value *member_ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
            llvm::Value *cast_member_ptr = builder.irbuilder().CreatePointerCast(member_ptr, member_val.known_value()->getType()->getPointerTo());
            builder.irbuilder().CreateStore(member_val.known_value(), cast_member_ptr);
            offset = builder.irbuilder().CreateAdd(offset, size);
          } else {
            PSI_ASSERT(member_val.is_unknown());
            LLVMValue member_type_val = builder.build_value(self.member_value(i)->type());
            PSI_ASSERT(member_type_val.is_known());
            std::pair<llvm::Value*,llvm::Value*> size_align = instruction_size_align(builder.irbuilder(), member_type_val.known_value());
            // align pointer to boundary
            offset = instruction_align(builder.irbuilder(), offset, size_align.second);
            llvm::Value *member_ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
            builder.create_memcpy(member_ptr, member_val.unknown_value(), size_align.first);
            offset = builder.irbuilder().CreateAdd(offset, size_align.first);
          }
        }

        return LLVMValue::known(storage);
      }
    }

    llvm::Constant* StructValue::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      PSI_ASSERT(!term.phantom());

      std::vector<llvm::Constant*> members;
      for (unsigned i = 0; i < self.n_members(); ++i)
        members.push_back(builder.build_constant(self.member_value(i)));

      return llvm::ConstantStruct::get(builder.llvm_context(), members, false);
    }

    FunctionalTermPtr<StructValue> Context::get_struct_value(ArrayPtr<Term*const> parameters) {
      return get_functional(StructValue(), parameters);
    }

    LLVMValue UnionType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return LLVMValue::known(type_value);

      LLVMIRBuilder& irbuilder = builder.irbuilder();
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.llvm_context());
      llvm::Value *size = llvm::ConstantInt::get(i64, 0);
      llvm::Value *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        std::pair<llvm::Value*, llvm::Value*> size_align = instruction_type_size_align(builder, self.member_type(i));
        size = instruction_max(irbuilder, size, size_align.first);
        align = instruction_max(irbuilder, align, size_align.second);
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);
      return LLVMMetatype::from_value(builder.irbuilder(), size, align);
    }

    llvm::Constant* UnionType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return type_value;

      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.llvm_context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_type_size_align(builder, self.member_type(i));
        size = constant_max(size, size_align.first);
        align = constant_max(align, size_align.second);
      }

      // size should always be a multiple of align
      size = constant_align(size, align);
      return LLVMMetatype::from_constant(size, align);
    }

    const llvm::Type* UnionType::llvm_type(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      std::vector<const llvm::Type*> member_types;
      for (std::size_t i = 0; i < self.n_members(); ++i) {
	if (const llvm::Type *ty = builder.build_type(self.member_type(i))) {
	  member_types.push_back(ty);
	} else {
          return NULL;
	}
      }

      return llvm::UnionType::get(builder.llvm_context(), member_types);
    }
  }
}
