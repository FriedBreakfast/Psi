#ifndef HPP_PSI_TVM_DERIVED
#define HPP_PSI_TVM_DERIVED

#include "Core.hpp"
#include "Functional.hpp"
#include "Primitive.hpp"

namespace Psi {
  namespace Tvm {
    class PointerType : public StatelessTerm {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;
      const llvm::Type* llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const PointerType*) : m_term(term) {}
	/// \brief Get the type being pointed to.
	Term* target_type() const {return m_term->parameter(0);}
      private:
	const FunctionalTerm *m_term;
      };

    private:
      static llvm::Constant* llvm_value(LLVMConstantBuilder&);
    };

    class ArrayType : public StatelessTerm {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;
      const llvm::Type* llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const;

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

    class ArrayValue : public StatelessTerm, public ValueTerm {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;

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

    class AggregateType : public StatelessTerm {
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
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;
      const llvm::Type* llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const;
    };

    class StructValue : public StatelessTerm, public ValueTerm {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;

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
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;
      const llvm::Type* llvm_type(LLVMConstantBuilder&, FunctionalTerm&) const;

      class Access : public AggregateType::Access {
      public:
        Access(const FunctionalTerm *term, const UnionType* backend) : AggregateType::Access(term, backend) {}

        int index_of_type(Term *type);
        bool contains_type(Term *type);
      };
    };

    class UnionValue : public StatelessTerm, public ValueTerm {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      llvm::Constant* llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm&) const;

      class Access {
      public:
	Access(const FunctionalTerm *term, const UnionValue*) : m_term(term) {}
        FunctionalTermPtr<UnionType> type() const {return checked_cast_functional<UnionType>(m_term->parameter(0));}
        Term* value() const {return m_term->parameter(1);}
      private:
	const FunctionalTerm *m_term;
      };
    };
  }
}

#endif
