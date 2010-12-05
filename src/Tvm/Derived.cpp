#include "Derived.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"
#include "LLVMBuilder.hpp"
#include "Number.hpp"

#include <stdexcept>

#include <boost/optional.hpp>

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
      return FunctionalTypeResult(context.get_metatype().get(), false);
    }

    LLVMValue PointerType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return llvm_value_constant(builder, term);
    }

    LLVMValue PointerType::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm&) const {
      return Metatype::llvm_from_type(builder, llvm::Type::getInt8PtrTy(builder.context()));
    }

    LLVMType PointerType::llvm_type(LLVMValueBuilder& builder, FunctionalTerm&) const {
      return LLVMType::known(const_cast<llvm::PointerType*>(llvm::Type::getInt8PtrTy(builder.context())));
    }

    FunctionalTermPtr<PointerType> Context::get_pointer_type(Term* type) {
      return get_functional_v(PointerType(), type);
    }

    namespace {
      boost::optional<LLVMValue> type_size_align(LLVMValueBuilder& builder, Term *type) {
        PSI_ASSERT(!type->phantom());
        LLVMType llvm_type = builder.type(type);
        if (llvm_type.is_known()) {
          return Metatype::llvm_from_type(builder, llvm_type.type());
        } else if (llvm_type.is_empty()) {
          return Metatype::llvm_empty(builder);
        } else {
          PSI_ASSERT(llvm_type.is_unknown());
          return boost::none;
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
      boost::optional<std::pair<llvm::Constant*, llvm::Constant*> > constant_type_size_align(LLVMValueBuilder& builder, Term *type) {
        LLVMType llvm_type = builder.type(type);
        if (!llvm_type.is_empty()) {
          if (llvm_type.is_known()) {
            return std::make_pair(llvm::ConstantExpr::getSizeOf(llvm_type.type()),
                                  llvm::ConstantExpr::getAlignOf(llvm_type.type()));
          } else {
            PSI_ASSERT(llvm_type.is_unknown());
            LLVMValue llvm_value = builder.value(type);
            PSI_ASSERT(llvm_value.is_known());
            return constant_size_align(llvm::cast<llvm::Constant>(llvm_value.known_value()));
          }
        } else {
          return boost::none;
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
      boost::optional<std::pair<llvm::Value*, llvm::Value*> > instruction_type_size_align(LLVMFunctionBuilder& builder, Term *type) {
        LLVMType llvm_type = builder.type(type);
        if (!llvm_type.is_empty()) {
          if (llvm_type.is_known()) {
            return std::make_pair<llvm::Value*,llvm::Value*>
              (llvm::ConstantExpr::getSizeOf(llvm_type.type()),
               llvm::ConstantExpr::getAlignOf(llvm_type.type()));
          } else {
            PSI_ASSERT(llvm_type.is_unknown());
            LLVMValue llvm_value = builder.value(type);
            PSI_ASSERT(llvm_value.is_known());
            return instruction_size_align(builder.irbuilder(), llvm_value.known_value());
          }
        } else {
          return boost::none;
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

      return FunctionalTypeResult(context.get_metatype().get(), parameters[0]->phantom() || parameters[1]->phantom());
    }

    LLVMValue ArrayType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (boost::optional<LLVMValue> type_value = type_size_align(builder, &term))
        return *type_value;

      boost::optional<std::pair<llvm::Value*,llvm::Value*> > size_align =
        instruction_type_size_align(builder, self.element_type());
      if (size_align) {
        LLVMValue length_value = builder.value(self.length());
        PSI_ASSERT(length_value.is_known());
        llvm::Value *size = builder.irbuilder().CreateMul(size_align->first, length_value.known_value());
        return Metatype::llvm_runtime(builder, size, size_align->second);
      } else {
        return Metatype::llvm_empty(builder);
      }
    }

    LLVMValue ArrayType::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (boost::optional<LLVMValue> type_value = type_size_align(builder, &term))
        return *type_value;

      boost::optional<std::pair<llvm::Constant*,llvm::Constant*> > size_align =
        constant_type_size_align(builder, self.element_type());
      if (size_align) {
        LLVMValue length_value = builder.value(self.length());
        PSI_ASSERT(length_value.is_known());
        llvm::Constant *size = llvm::ConstantExpr::getMul(size_align->first, llvm::cast<llvm::Constant>(length_value.known_value()));
        return Metatype::llvm_from_constant(builder, size, size_align->second);
      } else {
        return Metatype::llvm_empty(builder);
      }
    }

    LLVMType ArrayType::llvm_type(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMType element_llvm_type = builder.type(self.element_type());
      LLVMValue length_value = builder.value(self.length());

      if (!length_value.is_known() || !element_llvm_type.is_known())
	return LLVMType::unknown();

      llvm::ConstantInt *length_llvm = llvm::dyn_cast<llvm::ConstantInt>(length_value.known_value());
      if (!length_llvm)
        return LLVMType::unknown();

      return LLVMType::known(llvm::ArrayType::get(element_llvm_type.type(), length_llvm->getZExtValue()));
    }

    FunctionalTermPtr<ArrayType> Context::get_array_type(Term* element_type, Term* length) {
      return get_functional_v(ArrayType(), element_type, length);
    }

    FunctionalTermPtr<ArrayType> Context::get_array_type(Term* element_type, std::size_t length) {
      Term* length_term = get_functional_v(ConstantInteger(IntegerType(false, 64), length)).get();
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

      return FunctionalTypeResult(context.get_array_type(parameters[0], parameters.size() - 1).get(), phantom);
    }

    LLVMValue ArrayValue::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMType array_type = builder.type(term.type());
      PSI_ASSERT(array_type.is_known());

      llvm::Value *array = llvm::UndefValue::get(array_type.type());

      for (std::size_t i = 0; i < self.length(); ++i) {
        LLVMValue element = builder.value(self.value(i));
        PSI_ASSERT(element.is_known());
        array = builder.irbuilder().CreateInsertValue(array, element.known_value(), i);
      }

      return LLVMValue::known(array);
    }

    LLVMValue ArrayValue::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMType array_type = builder.type(term.type());
      PSI_ASSERT(array_type.is_known());

      std::vector<llvm::Constant*> elements;

      for (std::size_t i = 0; i < self.length(); ++i) {
        LLVMValue element = builder.value(self.value(i));
        PSI_ASSERT(element.is_known());
        elements.push_back(llvm::cast<llvm::Constant>(element.known_value()));
      }

      return LLVMValue::known(llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(array_type.type()), elements));
    }

    LLVMType ArrayValue::llvm_type(LLVMValueBuilder&, FunctionalTerm&) const {
      PSI_FAIL("array value cannot be used as a type");
    }

    FunctionalTermPtr<ArrayValue> Context::get_array_value(Term* element_type, ArrayPtr<Term*const> elements) {
      ScopedTermPtrArray<> parameters(elements.size() + 1);
      parameters[0] = element_type;
      for (std::size_t i = 0; i < elements.size(); ++i)
        parameters[i+1] = elements[i];
      return get_functional(ArrayValue(), parameters.array()).get();
    }

    FunctionalTypeResult AggregateType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      bool phantom = false;
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (!parameters[i]->is_type())
          throw TvmUserError("members of an aggregate type must be types");
        phantom = phantom || parameters[i]->phantom();
      }

      return FunctionalTypeResult(context.get_metatype().get(), phantom);
    }

    LLVMValue StructType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (boost::optional<LLVMValue> type_value = type_size_align(builder, &term))
        return *type_value;

      PSI_ASSERT(self.n_members() > 0);

      LLVMIRBuilder& irbuilder = builder.irbuilder();
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Value *size = llvm::ConstantInt::get(i64, 0);
      llvm::Value *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        LLVMType param_type = builder.type(self.member_type(i));
        boost::optional<std::pair<llvm::Value*, llvm::Value*> > size_align =
          instruction_type_size_align(builder, self.member_type(i));
        if (size_align) {
          size = irbuilder.CreateAdd(instruction_align(irbuilder, size, size_align->second), size_align->first);
          align = instruction_max(irbuilder, align, size_align->second);
        }
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);
      return Metatype::llvm_runtime(builder, size, align);
    }

    LLVMValue StructType::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (boost::optional<LLVMValue> type_value = type_size_align(builder, &term))
        return *type_value;

      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        LLVMType param_type = builder.type(self.member_type(i));
        boost::optional<std::pair<llvm::Constant*, llvm::Constant*> > size_align =
          constant_type_size_align(builder, self.member_type(i));
        if (size_align) {
          size = llvm::ConstantExpr::getAdd(constant_align(size, size_align->second), size_align->first);
          align = constant_max(align, size_align->second);
        }
      }

      // size should always be a multiple of align
      size = constant_align(size, align);
      return Metatype::llvm_from_constant(builder, size, align);
    }

    LLVMType StructType::llvm_type(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      std::vector<const llvm::Type*> member_types;
      for (std::size_t i = 0; i < self.n_members(); ++i) {
	LLVMType param_result = builder.type(self.member_type(i));
	if (param_result.is_known()) {
	  member_types.push_back(param_result.type());
	} else if (!param_result.is_empty()) {
	  return LLVMType::unknown();
	}
      }

      return LLVMType::known(llvm::StructType::get(builder.context(), member_types));
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

      LLVMType ty = builder.type(term.type());
      if (ty.is_known()) {
        llvm::Value *result = llvm::UndefValue::get(ty.type());
        for (std::size_t i = 0; i < self.n_members(); ++i) {
          LLVMValue val = builder.value(self.member_value(i));
          PSI_ASSERT(val.is_known());
          result = builder.irbuilder().CreateInsertValue(result, val.known_value(), i);
        }
        return LLVMValue::known(result);
      } else if (ty.is_empty()) {
        return LLVMValue::empty();
      } else {
        PSI_ASSERT(ty.is_unknown());

        llvm::Value *storage = builder.create_alloca_for(term.type());

        llvm::Value *offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.context()), 0);
        for (std::size_t i = 0; i < self.n_members(); ++i) {
          LLVMValue member_val = builder.value(self.member_value(i));
          if (member_val.is_known()) {
            llvm::Constant *size = llvm::ConstantExpr::getSizeOf(member_val.known_value()->getType());
            llvm::Constant *align = llvm::ConstantExpr::getAlignOf(member_val.known_value()->getType());
            // align pointer to boundary
            offset = instruction_align(builder.irbuilder(), offset, align);
            llvm::Value *member_ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
            llvm::Value *cast_member_ptr = builder.irbuilder().CreatePointerCast(member_ptr, member_val.known_value()->getType()->getPointerTo());
            builder.irbuilder().CreateStore(member_val.known_value(), cast_member_ptr);
            offset = builder.irbuilder().CreateAdd(offset, size);
          } else if (member_val.is_unknown()) {
            LLVMValue member_type_val = builder.value(self.member_value(i)->type());
            PSI_ASSERT(member_type_val.is_known());
            std::pair<llvm::Value*,llvm::Value*> size_align = instruction_size_align(builder.irbuilder(), member_type_val.known_value());
            // align pointer to boundary
            offset = instruction_align(builder.irbuilder(), offset, size_align.second);
            llvm::Value *member_ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
            builder.irbuilder().CreateCall5(builder.llvm_memcpy(),
                                            member_ptr, member_val.unknown_value(),
                                            size_align.first, builder.llvm_align_zero(),
                                            llvm::ConstantInt::getFalse(builder.context()));
            offset = builder.irbuilder().CreateAdd(offset, size_align.first);
          } else {
            PSI_ASSERT(member_val.is_empty());
          }
        }

        return LLVMValue::known(storage);
      }
    }

    LLVMValue StructValue::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      PSI_ASSERT(!term.phantom());

      LLVMType ty = builder.type(term.type());
      if (ty.is_known()) {
        llvm::Constant *result = llvm::UndefValue::get(ty.type());
        for (unsigned i = 0; i < self.n_members(); ++i) {
          LLVMValue val = builder.value(self.member_value(i));
          PSI_ASSERT(val.is_known());
          result = llvm::ConstantExpr::getInsertValue(result, llvm::cast<llvm::Constant>(val.known_value()), &i, 1);
        }
        return LLVMValue::known(result);
      } else if (ty.is_empty()) {
        return LLVMValue::empty();
      } else {
        throw LLVMBuildError("structs containing constant values of unknown type are not currently supported");
      }
    }

    LLVMType StructValue::llvm_type(LLVMValueBuilder&, FunctionalTerm&) const {
      PSI_FAIL("struct value should not have been used as a type");
    }

    FunctionalTermPtr<StructValue> Context::get_struct_value(ArrayPtr<Term*const> parameters) {
      return get_functional(StructValue(), parameters);
    }

    LLVMValue UnionType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (boost::optional<LLVMValue> type_value = type_size_align(builder, &term))
        return *type_value;

      LLVMIRBuilder& irbuilder = builder.irbuilder();
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Value *size = llvm::ConstantInt::get(i64, 0);
      llvm::Value *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        LLVMType param_type = builder.type(self.member_type(i));
        boost::optional<std::pair<llvm::Value*, llvm::Value*> > size_align =
          instruction_type_size_align(builder, self.member_type(i));
        if (size_align) {
          size = instruction_max(irbuilder, size, size_align->first);
          align = instruction_max(irbuilder, align, size_align->second);
        }
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);
      return Metatype::llvm_runtime(builder, size, align);
    }

    LLVMValue UnionType::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (boost::optional<LLVMValue> type_value = type_size_align(builder, &term))
        return *type_value;

      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        LLVMType param_type = builder.type(self.member_type(i));
        boost::optional<std::pair<llvm::Constant*, llvm::Constant*> > size_align =
          constant_type_size_align(builder, self.member_type(i));
        if (size_align) {
          size = constant_max(size, size_align->first);
          align = constant_max(align, size_align->second);
        }
      }

      // size should always be a multiple of align
      size = constant_align(size, align);
      return Metatype::llvm_from_constant(builder, size, align);
    }

    LLVMType UnionType::llvm_type(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      std::vector<const llvm::Type*> member_types;
      for (std::size_t i = 0; i < self.n_members(); ++i) {
	LLVMType param_result = builder.type(self.member_type(i));
	if (param_result.is_known()) {
	  member_types.push_back(param_result.type());
	} else if (!param_result.is_empty()) {
	  return LLVMType::unknown();
	}
      }

      return LLVMType::known(llvm::UnionType::get(builder.context(), member_types));
    }
  }
}
