#include "Derived.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"
#include "LLVMBuilder.hpp"
#include "Number.hpp"

#include <stdexcept>

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    FunctionalTypeResult PointerType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 1)
	throw std::logic_error("pointer type takes one parameter");
      if (!parameters[0]->is_type())
        throw std::logic_error("pointer argument must be a type");
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

    bool PointerType::operator == (const PointerType&) const {
      return true;
    }

    std::size_t hash_value(const PointerType&) {
      return 0;
    }

    FunctionalTermPtr<PointerType> Context::get_pointer_type(Term* type) {
      return get_functional_v(PointerType(), type);
    }

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
    }

    FunctionalTypeResult ArrayType::type(Context& context, ArrayPtr<Term*const> parameters) const {
      if (parameters.size() != 2)
	throw std::logic_error("array type term takes two parameters");

      if (!parameters[0]->is_type())
	throw std::logic_error("first argument to array type term is not a type");

      if (parameters[1]->type() != context.get_integer_type(64, false))
	throw std::logic_error("second argument to array type term is not a 64-bit integer");

      return FunctionalTypeResult(context.get_metatype().get(), parameters[0]->phantom() || parameters[1]->phantom());
    }

    LLVMValue ArrayType::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMValue element_size_align = builder.value(self.element_type());
      LLVMValue length_value = builder.value(self.length());

      if (!element_size_align.is_known() || !length_value.is_known())
	throw std::logic_error("array length value or element size and alignment is not a known constant");

      std::pair<llvm::Value*,llvm::Value*> size_align =
        instruction_size_align(builder.irbuilder(), element_size_align.value());
      llvm::Value *size = builder.irbuilder().CreateMul(size_align.first, length_value.value());
      return Metatype::llvm_runtime(builder, size, size_align.second);
    }

    LLVMValue ArrayType::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMValue element_size_align = builder.value(self.element_type());
      LLVMValue length_value = builder.value(self.length());

      if (!element_size_align.is_known() || !length_value.is_known())
	throw std::logic_error("constant array length value or element type is not a known");

      std::pair<llvm::Constant*,llvm::Constant*> size_align =
        constant_size_align(llvm::cast<llvm::Constant>(element_size_align.value()));
      llvm::Constant *size = llvm::ConstantExpr::getMul(size_align.first, llvm::cast<llvm::Constant>(length_value.value()));
      return Metatype::llvm_from_constant(builder, size, size_align.second);
    }

    LLVMType ArrayType::llvm_type(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMType element_llvm_type = builder.type(self.element_type());
      LLVMValue length_value = builder.value(self.length());

      if (!length_value.is_known() || !element_llvm_type.is_known())
	return LLVMType::unknown();

      llvm::ConstantInt *length_llvm = llvm::cast<llvm::ConstantInt>(length_value.value());
      return LLVMType::known(llvm::ArrayType::get(element_llvm_type.type(), length_llvm->getZExtValue()));
    }

    bool ArrayType::operator == (const ArrayType&) const {
      return true;
    }

    std::size_t hash_value(const ArrayType&) {
      return 0;
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
        throw std::logic_error("array values require at least one parameter");

      if (!parameters[0]->is_type())
        throw std::logic_error("first argument to array value is not a type");

      bool phantom = false;
      for (std::size_t i = 1; i < parameters.size(); ++i) {
        if (parameters[i]->type() != parameters[0])
          throw std::logic_error("array value element is of the wrong type");
        phantom = phantom || parameters[i]->phantom();
      }

      PSI_ASSERT(phantom || !parameters[0]->phantom());

      return FunctionalTypeResult(context.get_array_type(parameters[0], parameters.size() - 1).get(), phantom);
    }

    LLVMValue ArrayValue::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMType array_type = builder.type(term.type());
      if (!array_type.is_known())
        throw std::logic_error("array element type not known");

      llvm::Value *array = llvm::UndefValue::get(array_type.type());

      for (std::size_t i = 0; i < self.length(); ++i) {
        LLVMValue element = builder.value(self.value(i));
        if (!element.is_known())
          throw std::logic_error("array element value not known");
        array = builder.irbuilder().CreateInsertValue(array, element.value(), i);
      }

      return LLVMValue::known(array);
    }

    LLVMValue ArrayValue::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      Access self(&term, this);

      LLVMType array_type = builder.type(term.type());
      if (!array_type.is_known())
        throw std::logic_error("array element type not known");

      std::vector<llvm::Constant*> elements;

      for (std::size_t i = 0; i < self.length(); ++i) {
        LLVMValue element = builder.value(self.value(i));
        if (!element.is_known())
          throw std::logic_error("array element value not known");
        elements.push_back(llvm::cast<llvm::Constant>(element.value()));
      }

      return LLVMValue::known(llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(array_type.type()), elements));
    }

    LLVMType ArrayValue::llvm_type(LLVMValueBuilder&, FunctionalTerm&) const {
      throw std::logic_error("array value cannot be used as a type");
    }

    bool ArrayValue::operator == (const ArrayValue&) const {
      return true;
    }

    std::size_t hash_value(const ArrayValue&) {
      return 0;
    }

    FunctionalTermPtr<ArrayValue> Context::get_constant_array(Term* element_type, ArrayPtr<Term*const> elements) {
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
          throw std::logic_error("members of an aggregate type must be types");
        phantom = phantom || parameters[i]->phantom();
      }

      return FunctionalTypeResult(context.get_metatype().get(), phantom);
    }

    bool AggregateType::operator == (const AggregateType&) const {
      return true;
    }

    std::size_t hash_value(const AggregateType&) {
      return 0;
    }

#if 0
    LLVMValueBuilder::Constant StructType::llvm_value_constant(LLVMValueBuilder& builder, Term* term) const {
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
      LLVMIRBuilder& irbuilder = builder.irbuilder();
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

    LLVMValueBuilder::Type StructType::llvm_type(LLVMValueBuilder& builder, Term *term) const {
      std::vector<const llvm::Type*> member_types;
      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMValueBuilder::Type param_result = builder.type(param);
	if (param_result.known()) {
	  member_types.push_back(param_result.type());
	} else if (!param_result.empty()) {
	  return LLVMValueBuilder::type_unknown();
	}
      }

      if (!member_types.empty()) {
	return LLVMValueBuilder::type_known(llvm::StructType::get(builder.context(), member_types));
      } else {
	return LLVMValueBuilder::type_empty();
      }
    }

    void StructType::validate_parameters(Context&, std::size_t n_parameters, Term *const* parameters) const {
      for (std::size_t i = 0; i < n_parameters; ++i) {
	if (parameters[i]->proto().category() == ProtoTerm::term_value)
	  throw std::logic_error("first argument to array type term is not a type or metatype");
      }
    }

    LLVMFunctionBuilder::Result UnionType::llvm_value_instruction(LLVMFunctionBuilder& builder, Term* term) const {
      LLVMIRBuilder& irbuilder = builder.irbuilder();
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

    LLVMValueBuilder::Constant UnionType::llvm_value_constant(LLVMValueBuilder& builder, Term* term) const {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(builder.context());
      llvm::Constant *size = llvm::ConstantInt::get(i64, 0);
      llvm::Constant *align = llvm::ConstantInt::get(i64, 1);

      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMValueBuilder::Constant param_result = builder.constant(param);
	PSI_ASSERT(param_result.known(), "Value of metatype is not known");
	std::pair<llvm::Constant*, llvm::Constant*> size_align = constant_size_align(param_result.value());	
	size = constant_max(size, size_align.first);
	align = constant_max(size, size_align.second);
      }

      // size must be a multiple of align
      size = constant_align(size, align);

      return Metatype::llvm_value(size, align);
    }

    LLVMValueBuilder::Type UnionType::llvm_type(LLVMValueBuilder& builder, Term* term) const {
      std::vector<const llvm::Type*> lm;
      for (std::size_t i = 0; i < term->n_parameters(); ++i) {
	Term *param = term->parameter(i);
	LLVMValueBuilder::Type param_result = builder.type(param);
	if (param_result.known()) {
	  lm.push_back(param_result.type());
	} else if (!param_result.empty()) {
	  return LLVMValueBuilder::type_unknown();
	}
      }

      if (!lm.empty())
	return LLVMValueBuilder::type_known(llvm::UnionType::get(&lm[0], lm.size()));
      else
	return LLVMValueBuilder::type_empty();
    }

    void UnionType::validate_parameters(Context&, std::size_t n_parameters, Term *const* parameters) const {
      for (std::size_t i = 0; i < n_parameters; ++i) {
	if (parameters[i]->proto().category() == ProtoTerm::term_value)
	  throw std::logic_error("first argument to array type term is not a type or metatype");
      }
    }
#endif
  }
}
