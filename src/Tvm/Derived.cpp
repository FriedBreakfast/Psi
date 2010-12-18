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
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>

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
      return LLVMValue::known(llvm_value(builder));
    }

    llvm::Constant* PointerType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm&) const {
      return llvm_value(builder);
    }

    llvm::Constant* PointerType::llvm_value(LLVMConstantBuilder& builder) {
      return LLVMMetatype::from_type(builder, llvm::Type::getInt8PtrTy(builder.llvm_context()));
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
          return LLVMMetatype::from_type(builder, llvm_type);
        } else {
          return NULL;
        }
      }

      /**
       * Compute size and alignment of a type from its term.
       */
      class ConstantSizeAlign {
      public:
        ConstantSizeAlign(LLVMConstantBuilder *builder, Term *type) {
          if (const llvm::Type *ty = builder->build_type(type)) {
            const llvm::TargetData *td = builder->llvm_target_machine()->getTargetData();
            m_size = td->getTypeAllocSize(ty);
            m_align = td->getPrefTypeAlignment(ty);
          } else {
            llvm::Constant *metatype_val = builder->build_constant(type);
            m_size = LLVMMetatype::to_size_constant(metatype_val);
            m_align = LLVMMetatype::to_align_constant(metatype_val);
          }
        }

        BigInteger size() const {return m_size;}
        BigInteger align() const {return m_align;}

      private:
        BigInteger m_size, m_align;
      };

      /**
       *
       */
      class InstructionSizeAlign {
      public:
        InstructionSizeAlign(LLVMFunctionBuilder *builder, Term *type)
          : m_builder(builder),
            m_type(type),
            m_llvm_type(builder->build_type(type)),
            m_llvm_size(0),
            m_llvm_align(0) {
        }

      public:
        llvm::Value* size() {
          if (!m_llvm_size) {
            if (m_llvm_type) {
              m_llvm_size = llvm::ConstantInt::get(m_builder->size_type(), m_builder->type_size(m_llvm_type));
            } else {
              m_llvm_size = LLVMMetatype::to_size_value(*m_builder, build_value());
            }
          }

          return m_llvm_size;
        }

        llvm::Value* align() {
          if (!m_llvm_align) {
            if (m_llvm_type)
              m_llvm_align = llvm::ConstantInt::get(m_builder->size_type(), m_builder->type_alignment(m_llvm_type));
            else
              m_llvm_align = LLVMMetatype::to_align_value(*m_builder, build_value());
          }

          return m_llvm_align;
        }

      private:
        LLVMFunctionBuilder *m_builder;
        Term *m_type;
        const llvm::Type *m_llvm_type;
        llvm::Value *m_llvm_size, *m_llvm_align;
        llvm::Value *m_llvm_value;

        llvm::Value* build_value() {
          if (!m_llvm_value)
            m_llvm_value = m_builder->build_known_value(m_type);
          return m_llvm_value;
        }
      };

      /**
       * Align an offset to a specified alignment, which must be a
       * power of two.
       */
      BigInteger constant_align(const BigInteger& offset, const BigInteger& align) {
        BigInteger x = align - 1;
        return (offset + x) & ~x;
      }

      /**
       * Compute the maximum of two values.
       */
      llvm::Value* instruction_max(LLVMIRBuilder& irbuilder, llvm::Value *left, llvm::Value *right) {
	llvm::Value *cmp = irbuilder.CreateICmpULT(left, right);
	return irbuilder.CreateSelect(cmp, left, right);
      }

      /*
       * Align a size to a boundary. The formula is: <tt>(size + align
       * - 1) & ~(align - 1)</tt>. <tt>align</tt> must be a power of two.
       */
      llvm::Value* instruction_align(LLVMIRBuilder& irbuilder, llvm::Value* size, llvm::Value* align) {
	llvm::Constant* one = llvm::ConstantInt::get(llvm::cast<llvm::IntegerType>(size->getType()), 1);
	llvm::Value* a = irbuilder.CreateSub(align, one);
	llvm::Value* b = irbuilder.CreateAdd(size, a);
	llvm::Value* c = irbuilder.CreateNot(align);
	return irbuilder.CreateAnd(b, c);
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

      InstructionSizeAlign element(&builder, self.element_type());
      llvm::Value *length = builder.build_known_value(self.length());
      llvm::Value *array_size = builder.irbuilder().CreateMul(element.size(), length);
      return LLVMMetatype::from_value(builder, array_size, element.align());
    }

    llvm::Constant* ArrayType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant* type_value = type_size_align(builder, &term))
        return type_value;

      ConstantSizeAlign element(&builder, self.element_type());
      BigInteger length = builder.build_constant_integer(self.length());
      return LLVMMetatype::from_constant(builder, element.size() * length, element.align());
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
        llvm::Value *storage = builder.create_alloca_for(term.type());

        InstructionSizeAlign element(&builder, self.element_type());
        llvm::Value *offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(builder.llvm_context()), 0);
        for (std::size_t i = 0; i < self.length(); ++i) {
          llvm::Value *ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
          builder.create_store(ptr, self.value(i));
        }

        return LLVMValue::unknown(storage);
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
        InstructionSizeAlign member(&builder, self.member_type(i));
        size = irbuilder.CreateAdd(instruction_align(irbuilder, size, member.align()), member.size());
        align = instruction_max(irbuilder, align, member.align());
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);
      return LLVMMetatype::from_value(builder, size, align);
    }

    llvm::Constant* StructType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return type_value;

      BigInteger size = 0, align = 1;

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        ConstantSizeAlign member(&builder, self.member_type(i));
        size = constant_align(size, member.align()) + member.size();
        align = std::max(align, member.align());
      }

      // size should always be a multiple of align
      size = constant_align(size, align);
      return LLVMMetatype::from_constant(builder, size, align);
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
          InstructionSizeAlign member_type(&builder, self.member_value(i)->type());
          offset = instruction_align(builder.irbuilder(), offset, member_type.align());
          llvm::Value *ptr = builder.irbuilder().CreateInBoundsGEP(storage, offset);
          builder.create_store(ptr, self.member_value(i));
          offset = builder.irbuilder().CreateAdd(offset, member_type.size());
        }

        return LLVMValue::unknown(storage);
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
        InstructionSizeAlign member(&builder, self.member_type(i));
        size = instruction_max(irbuilder, size, member.size());
        align = instruction_max(irbuilder, align, member.align());
      }

      // size should always be a multiple of align
      size = instruction_align(irbuilder, size, align);
      return LLVMMetatype::from_value(builder, size, align);
    }

    llvm::Constant* UnionType::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      if (llvm::Constant *type_value = type_size_align(builder, &term))
        return type_value;

      BigInteger size = 0, align = 1;

      for (std::size_t i = 0; i < self.n_members(); ++i) {
        ConstantSizeAlign member(&builder, self.member_type(i));
        size = std::max(size, member.size());
        align = std::max(align, member.align());
      }

      // size should always be a multiple of align
      size = constant_align(size, align);
      return LLVMMetatype::from_constant(builder, size, align);
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

    int UnionType::Access::index_of_type(Term *type) {
      for (std::size_t i = 0; i < n_members(); ++i) {
        if (type == member_type(i))
          return i;
      }
      return -1;
    }

    bool UnionType::Access::contains_type(Term *type) {
      return index_of_type(type) >= 0;
    }

    FunctionalTypeResult UnionValue::type(Context&, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 2)
        throw TvmUserError("c_union takes two parameters");

      FunctionalTermPtr<UnionType> type = dynamic_cast_functional<UnionType>(parameters[0]);
      if (!type)
        throw TvmUserError("first argument to c_union must be a union type");

      if (!type.backend().contains_type(parameters[1]->type()))
        throw TvmUserError("second argument to c_union must correspond to a member of the specified union type");

      return FunctionalTypeResult(type, parameters[0]->phantom() || parameters[1]->phantom());
    }

    LLVMValue UnionValue::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      PSI_ASSERT(!term.phantom());

      if (const llvm::Type* ty = builder.build_type(term.type())) {
        llvm::Value *undef = llvm::UndefValue::get(ty);
        llvm::Value *val = builder.build_known_value(self.value());
        int index = llvm::cast<llvm::UnionType>(ty)->getElementTypeIndex(val->getType());
        llvm::Value *result = builder.irbuilder().CreateInsertValue(undef, val, index);
        return LLVMValue::known(result);
      } else {
        llvm::Value *storage = builder.create_alloca_for(term.type());
        builder.create_store(storage, self.value());
        return LLVMValue::unknown(storage);
      }
    }

    llvm::Constant* UnionValue::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      PSI_ASSERT(!term.phantom());

      const llvm::Type *ty = builder.build_type(term.type());
      llvm::Constant *val = builder.build_constant(self.value());
      return llvm::ConstantUnion::get(llvm::cast<llvm::UnionType>(ty), val);
    }
  }
}
