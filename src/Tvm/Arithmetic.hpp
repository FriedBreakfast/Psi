#ifndef HPP_PSI_TVM_ARITHMETIC
#define HPP_PSI_TVM_ARITHMETIC

#include "Core.hpp"
#include "Primitive.hpp"

namespace Psi {
  namespace Tvm {
    class ArithmeticOperation {
    public:
      static Term* integer_binary_op_type(Context& context, ArrayPtr<Term*const> parameters);
      static LLVMValue binary_op_constant(LLVMValueBuilder& builder, FunctionalTerm& term,
                                          llvm::Constant* (*callback) (llvm::Constant*, llvm::Constant*));
      static LLVMValue binary_op_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term,
                                             llvm::Value* (LLVMFunctionBuilder::IRBuilder::*) (llvm::Value*,llvm::Value*,const llvm::Twine&));

      class BinaryAccess {
      public:
        BinaryAccess(const FunctionalTerm* term, const void*) : m_term(term) {}
        Term* lhs() const {return m_term->parameter(0);}
        Term* rhs() const {return m_term->parameter(1);}

      private:
        const FunctionalTerm *m_term;
      };
    };

    class IntegerAdd : public StatelessOperand {
    public:
      Term* type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerSubtract : public StatelessOperand {
    public:
      Term* type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerMultiply : public StatelessOperand {
    public:
      Term* type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };

    class IntegerDivide : public StatelessOperand {
    public:
      Term* type(Context& context, ArrayPtr<Term*const> parameters) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder& builder, FunctionalTerm& term) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

      typedef ArithmeticOperation::BinaryAccess Access;
    };
  }
}

#endif
