#ifndef HPP_PSI_TVM_DERIVED
#define HPP_PSI_TVM_DERIVED

#include "Core.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"

namespace Psi {
  namespace Tvm {
    class PointerType : public StatelessOperand {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const PointerType*) : m_term(term) {}
	/// \brief Get the type being pointed to.
	Term* target_type() const {return m_term->parameter(0);}
      private:
	const FunctionalTerm *m_term;
      };
    };

    class ArrayType : public StatelessOperand {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const ArrayType*) : m_term(term) {}
	/// \brief Get the type being pointed to.
	Term* element_type() const {return m_term->parameter(0);}
        Term* length() const {return m_term->parameter(1);}
      private:
	const FunctionalTerm *m_term;
      };
    };

    class ArrayValue : public StatelessOperand {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const ArrayValue*) : m_term(term) {}
	/// \brief Get the type being pointed to.
        std::size_t length() const {return m_term->n_parameters() - 1;}
        Term* element_type() const {return m_term->parameter(0);}
        Term* value(std::size_t n) {return m_term->parameter(n+1);}
      private:
	const FunctionalTerm *m_term;
      };
    };

    class AggregateType : public StatelessOperand {
    public:
      class Access {
      public:
        Access(const FunctionalTerm *term, const AggregateType*) : m_term(term) {}
        std::size_t n_members() const {return m_term->n_parameters();}
        Term* member_type(std::size_t i) const {return m_term->parameter(i);}

      private:
        const FunctionalTerm *m_term;
      };

      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
    };

    class StructType : public AggregateType {
    public:
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;
    };

    class StructValue : public StatelessOperand {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const StructValue*) : m_term(term) {}
        std::size_t n_members() const {return m_term->n_parameters();}
        Term* member_value(std::size_t n) const {return m_term->parameter(n);}
      private:
	const FunctionalTerm *m_term;
      };
    };

    class UnionType : public AggregateType {
    public:
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;
    };

    class UnionValue {
    public:
      UnionValue(unsigned which);
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;
      bool operator == (const UnionValue&);
      friend std::size_t hash_value(const UnionValue&);

      class Access {
      public:
	Access(const FunctionalTerm *term, const UnionValue* self) : m_term(term), m_self(self) {}
        std::size_t which() const {return m_self->m_which;}
        Term* type() const {return m_term->parameter(0);}
        Term* value() const {return m_term->parameter(1);}
      private:
	const FunctionalTerm *m_term;
        const UnionValue *m_self;
      };

    private:
      unsigned m_which;
    };
  }
}

#endif
