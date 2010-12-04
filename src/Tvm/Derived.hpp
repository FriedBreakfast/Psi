#ifndef HPP_PSI_TVM_DERIVED
#define HPP_PSI_TVM_DERIVED

#include "Core.hpp"
#include "Functional.hpp"

namespace Psi {
  namespace Tvm {
    class PointerType {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;
      bool operator == (const PointerType&) const;
      friend std::size_t hash_value(const PointerType&);

      class Access {
      public:
	Access(const FunctionalTerm *term, const PointerType*) : m_term(term) {}
	/// \brief Get the type being pointed to.
	Term* target_type() const {return m_term->parameter(0);}
      private:
	const FunctionalTerm *m_term;
      };
    };

    class ArrayType {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;
      bool operator == (const ArrayType&) const;
      friend std::size_t hash_value(const ArrayType&);

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

    class ArrayValue {
    public:
      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, FunctionalTerm&) const;
      bool operator == (const ArrayValue&) const;
      friend std::size_t hash_value(const ArrayValue&);

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

    class AggregateType {
    public:
      class Access {
      public:
        Access(const FunctionalTerm *term, const AggregateType*) : m_term(term) {}
        std::size_t n_members() const {return m_term->n_parameters();}
        Term* member(std::size_t i) const {return m_term->parameter(i);}

      private:
        const FunctionalTerm *m_term;
      };

      FunctionalTypeResult type(Context&, ArrayPtr<Term*const>) const;
      bool operator == (const AggregateType&) const;
      friend std::size_t hash_value(const AggregateType&);
    };

#if 0
    class StructType : public AggregateType {
    public:
      static Term* create(Context& context, std::size_t n_members, Term *const* elements);

    private:
      virtual ProtoTerm* clone() const;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
      virtual LLVMValueBuilder::Constant llvm_value_constant(LLVMValueBuilder&, Term*) const;
      virtual LLVMValueBuilder::Type llvm_type(LLVMValueBuilder&, Term*) const;
      virtual void validate_parameters(Context& context, std::size_t n_parameters, Term *const* parameters) const;
    };

    class StructValue : public Value {
    public:
      static Term* create(Term *type, std::size_t n_elements, Term *const* elements);
      virtual Term* type(Context *context, std::size_t n_parameters, Term *const* parameters) const;

    private:
      virtual bool equals_internal(const ProtoTerm& other) const = 0;
      virtual std::size_t hash_internal() const = 0;
      virtual ProtoTerm* clone() const = 0;
      virtual LLVMValueBuilder::Constant llvm_value_constant(LLVMValueBuilder&, Term*) const;
    };

    class UnionType : public AggregateType {
    public:
      static Term* create(Context& context, std::size_t n_members, Type *const* members);

    private:
      virtual ProtoTerm* clone() const;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
      virtual LLVMValueBuilder::Constant llvm_value_constant(LLVMValueBuilder&, Term*) const;
      virtual LLVMValueBuilder::Type llvm_type(LLVMValueBuilder&, Term*) const;
      virtual void validate_parameters(Context& context, std::size_t n_parameters, Term *const* parameters) const;
    };

    class UnionValue : public Value {
    public:
      UnionValue(int which);
      static Term* create(Term *type, int which, Term *value);
      virtual Term* type(Context *context, std::size_t n_parameters, Term *const* parameters) const;
      int which() {return m_which;}

    private:
      int m_which;

      virtual bool equals_internal(const ProtoTerm& other) const = 0;
      virtual std::size_t hash_internal() const = 0;
      virtual ProtoTerm* clone() const = 0;
      virtual LLVMValueBuilder::Constant llvm_value_constant(LLVMValueBuilder&, Term*) const;
    };
#endif
  }
}

#endif
