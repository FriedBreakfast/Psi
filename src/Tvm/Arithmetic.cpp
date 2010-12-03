#include "Arithmetic.hpp"
#include "Number.hpp"

#include <llvm/Constant.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    Term* ArithmeticOperation::integer_binary_op_type(Context&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw std::logic_error("binary arithmetic operation expects two operands");

      Term* type = parameters[0]->type();
      if (type != parameters[1]->type())
        throw std::logic_error("type mismatch between operands to binary arithmetic operation");

      FunctionalTermPtr<IntegerType> int_type = dynamic_cast_functional<IntegerType>(type);
      if (!int_type)
        throw std::logic_error("parameters to integer binary arithmetic operation were not integers");

      return int_type.get();
    }

    LLVMValue ArithmeticOperation::binary_op_constant(LLVMValueBuilder& builder, FunctionalTerm& term, llvm::Constant* (*callback) (llvm::Constant*, llvm::Constant*)) {
      BinaryAccess self(&term, NULL);
      LLVMValue lhs = builder.value(self.lhs());
      LLVMValue rhs = builder.value(self.rhs());

      if (!lhs.is_known() || !rhs.is_known())
        throw std::logic_error("cannot perform arithmetic on unknown operands");

      return LLVMValue::known(callback(llvm::cast<llvm::Constant>(lhs.value()),
                                       llvm::cast<llvm::Constant>(rhs.value())));
    }

    LLVMValue ArithmeticOperation::binary_op_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term, llvm::Value* (LLVMFunctionBuilder::IRBuilder::*callback) (llvm::Value*,llvm::Value*,const llvm::Twine&)) {
      BinaryAccess self(&term, NULL);
      LLVMValue lhs = builder.value(self.lhs());
      LLVMValue rhs = builder.value(self.rhs());

      if (!lhs.is_known() || !rhs.is_known())
        throw std::logic_error("cannot perform arithmetic on unknown operands");

      return LLVMValue::known((builder.irbuilder().*callback)(lhs.value(), rhs.value(), ""));
    }

    Term* IntegerAdd::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMType IntegerAdd::llvm_type(LLVMValueBuilder&, Term&) const {
      throw std::logic_error("arithmetic operations cannot be used as types");
    }

    LLVMValue IntegerAdd::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMFunctionBuilder::IRBuilder::CreateAdd);
    }

    LLVMValue IntegerAdd::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, llvm::ConstantExpr::getAdd);
    }

    Term* IntegerSubtract::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMType IntegerSubtract::llvm_type(LLVMValueBuilder&, Term&) const {
      throw std::logic_error("arithmetic operations cannot be used as types");
    }

    LLVMValue IntegerSubtract::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMFunctionBuilder::IRBuilder::CreateSub);
    }

    LLVMValue IntegerSubtract::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, llvm::ConstantExpr::getSub);
    }

    Term* IntegerMultiply::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMType IntegerMultiply::llvm_type(LLVMValueBuilder&, Term&) const {
      throw std::logic_error("arithmetic operations cannot be used as types");
    }

    LLVMValue IntegerMultiply::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMFunctionBuilder::IRBuilder::CreateMul);
    }

    LLVMValue IntegerMultiply::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, llvm::ConstantExpr::getMul);
    }

    Term* IntegerDivide::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMType IntegerDivide::llvm_type(LLVMValueBuilder&, Term&) const {
      throw std::logic_error("arithmetic operations cannot be used as types");
    }

    LLVMValue IntegerDivide::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      bool is_signed = checked_cast_functional<IntegerType>(term.type()).backend().is_signed();
      if (is_signed)
        return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMFunctionBuilder::IRBuilder::CreateSDiv);
      else
        return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMFunctionBuilder::IRBuilder::CreateUDiv);
    }

    LLVMValue IntegerDivide::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const {
      bool is_signed = checked_cast_functional<IntegerType>(term.type()).backend().is_signed();
      if (is_signed)
        return ArithmeticOperation::binary_op_constant(builder, term, llvm::ConstantExpr::getSDiv);
      else
        return ArithmeticOperation::binary_op_constant(builder, term, llvm::ConstantExpr::getUDiv);
    }
  }
}
