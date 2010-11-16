#ifndef HPP_PSI_TVM_ARITHMETIC
#define HPP_PSI_TVM_ARITHMETIC

#include "Core.hpp"
#include "Primitive.hpp"

namespace Psi {
  namespace Tvm {
    class ArithmeticOperation {
    public:
      static TermPtr<> integer_binary_op_type(Context& context, TermRefArray<> parameters);
      static LLVMValue binary_op_constant(LLVMValueBuilder& builder, FunctionalTerm& term, llvm::Constant* (*callback) (llvm::Constant*, llvm::Constant*));
      static LLVMValue binary_op_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term, llvm::Value* (LLVMFunctionBuilder::IRBuilder::*) (llvm::Value*,llvm::Value*,const llvm::Twine&));
      static LLVMType no_type();

      class BinaryAccess {
      public:
        BinaryAccess(const FunctionalTerm* term, const void*) : m_term(term) {}
        TermPtr<> lhs() const {return m_term->parameter(0);}
        TermPtr<> rhs() const {return m_term->parameter(1);}

      private:
        const FunctionalTerm *m_term;
      };
    };

    class IntegerAdd : public StatelessOperand {
    public:
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerSubtract : public StatelessOperand {
    public:
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerMultiply : public StatelessOperand {
    public:
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerDivide : public StatelessOperand {
    public:
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };
  }
}

#endif
