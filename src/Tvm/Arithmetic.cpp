#include "Arithmetic.hpp"
#include "Number.hpp"

#include <functional>

#include <llvm/Constant.h>
#include <llvm/Support/IRBuilder.h>

namespace Psi {
  namespace Tvm {
    FunctionalTypeResult ArithmeticOperation::integer_binary_op_type(Context&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("binary arithmetic operation expects two operands");

      Term* type = parameters[0]->type();
      if (type != parameters[1]->type())
        throw TvmUserError("type mismatch between operands to binary arithmetic operation");

      FunctionalTermPtr<IntegerType> int_type = dynamic_cast_functional<IntegerType>(type);
      if (!int_type)
        throw TvmUserError("parameters to integer binary arithmetic operation were not integers");

      return FunctionalTypeResult(int_type, parameters[0]->phantom() || parameters[1]->phantom());
    }

    template<typename T>
    llvm::Constant* ArithmeticOperation::binary_op_constant(LLVMConstantBuilder& builder, FunctionalTerm& term, T op) {
      BinaryAccess self(&term, NULL);
      FunctionalTermPtr<IntegerType> type = checked_cast_functional<IntegerType>(term.type());
      BigInteger lhs = builder.build_constant_integer(self.lhs());
      BigInteger rhs = builder.build_constant_integer(self.rhs());
      BigInteger result = op(lhs, rhs);
      llvm::APInt result_llvm = LLVMConstantBuilder::bigint_to_apint(result, type.backend().n_bits(), type.backend().is_signed(), true);
      const llvm::IntegerType *type_llvm = llvm::IntegerType::get(builder.llvm_context(), type.backend().n_bits());
      return llvm::ConstantInt::get(type_llvm, result_llvm);
    }

    LLVMValue ArithmeticOperation::binary_op_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term, llvm::Value* (LLVMIRBuilder::*callback) (llvm::Value*,llvm::Value*,const llvm::Twine&)) {
      BinaryAccess self(&term, NULL);
      LLVMValue lhs = builder.build_value(self.lhs());
      LLVMValue rhs = builder.build_value(self.rhs());
      PSI_ASSERT(lhs.is_known() && rhs.is_known());
      return LLVMValue::known((builder.irbuilder().*callback)(lhs.known_value(), rhs.known_value(), ""));
    }

    FunctionalTypeResult IntegerAdd::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMValue IntegerAdd::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMIRBuilder::CreateAdd);
    }

    llvm::Constant* IntegerAdd::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, std::plus<BigInteger>());
    }

    FunctionalTypeResult IntegerSubtract::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMValue IntegerSubtract::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMIRBuilder::CreateSub);
    }

    llvm::Constant* IntegerSubtract::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, std::minus<BigInteger>());
    }

    FunctionalTypeResult IntegerMultiply::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMValue IntegerMultiply::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMIRBuilder::CreateMul);
    }

    llvm::Constant* IntegerMultiply::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, std::multiplies<BigInteger>());
    }

    FunctionalTypeResult IntegerDivide::type(Context& context, ArrayPtr<Term*const> parameters) const {
      return ArithmeticOperation::integer_binary_op_type(context, parameters);
    }

    LLVMValue IntegerDivide::llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const {
      bool is_signed = checked_cast_functional<IntegerType>(term.type()).backend().is_signed();
      if (is_signed)
        return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMIRBuilder::CreateSDiv);
      else
        return ArithmeticOperation::binary_op_instruction(builder, term, &LLVMIRBuilder::CreateUDiv);
    }

    llvm::Constant* IntegerDivide::llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const {
      return ArithmeticOperation::binary_op_constant(builder, term, std::divides<BigInteger>());
    }
  }
}
