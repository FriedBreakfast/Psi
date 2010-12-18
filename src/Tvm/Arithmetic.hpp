#ifndef HPP_PSI_TVM_ARITHMETIC
#define HPP_PSI_TVM_ARITHMETIC

#include "Core.hpp"
#include "Primitive.hpp"

namespace Psi {
  namespace Tvm {
    class ArithmeticOperation {
    public:
      static FunctionalTypeResult integer_binary_op_type(Context& context, ArrayPtr<Term*const> parameters);
      template<typename T>
      static llvm::Constant* binary_op_constant(LLVMConstantBuilder& builder, FunctionalTerm& term, T op);
      static LLVMValue binary_op_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term,
                                             llvm::Value* (LLVMIRBuilder::*) (llvm::Value*,llvm::Value*,const llvm::Twine&));

      class BinaryAccess {
      public:
        BinaryAccess(const FunctionalTerm* term, const void*) : m_term(term) {}
        Term* lhs() const {return m_term->parameter(0);}
        Term* rhs() const {return m_term->parameter(1);}

      private:
        const FunctionalTerm *m_term;
      };
    };

    class IntegerAdd : public ValueTerm, public StatelessTerm {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerSubtract : public ValueTerm, public StatelessTerm {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerMultiply : public ValueTerm, public StatelessTerm {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerDivide : public ValueTerm, public StatelessTerm {
    public:
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };
  }
}

#endif
