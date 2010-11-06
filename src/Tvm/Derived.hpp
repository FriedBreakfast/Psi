#ifndef HPP_PSI_TVM_DERIVED
#define HPP_PSI_TVM_DERIVED

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    class PointerType {
    public:
      TermPtr<> type(Context&, TermRefArray<>) const;
      LLVMValue llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm&) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder&, FunctionalTerm&) const;
      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      bool operator == (const PointerType&) const;
      friend std::size_t hash_value(const PointerType&);

      TermPtr<> target_type(FunctionalTerm& term) const;
    };

#if 0
    class AggregateType : public ProtoTerm {
    protected:
      enum Slots {
	slot_members_start=TemplateType::slot_max
      };

    public:
      std::size_t n_members() {return use_slots() - slot_members_start;}
      TermType *member(std::size_t n) {return use_get<TermType>(slot_members_start+n);}

    private:
      template<typename Derived> class Initializer;
    protected:
      template<typename Derived> static Derived* create(Context *context, std::size_t n_parameters,
							std::size_t n_members, Type *const* members);
      AggregateType(const UserInitializer& ui, Context *context,
		    std::size_t n_parameters, std::size_t n_members, Type *const* members);

      virtual bool constant_for(Term *const* parameters);
      std::vector<LLVMBuilderValue> build_llvm_member_values(LLVMBuilder& builder, AppliedType *applied);
      std::vector<LLVMBuilderType> build_llvm_member_types(LLVMBuilder& builder, AppliedType *applied);
    };

    class ArrayType : public DerivedType {
    public:
      static Term* create(Context& context, Term *element_type, Term *size);

    private:
      virtual ProtoTerm* clone() const;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
      virtual LLVMValueBuilder::Constant llvm_value_constant(LLVMValueBuilder&, Term*) const;
      virtual LLVMValueBuilder::Type llvm_type(LLVMValueBuilder&, Term*) const;
      virtual void validate_parameters(Context& context, std::size_t n_parameters, Term *const* parameters) const;
    };

    class ArrayValue : public Value {
    public:
      static Term* create(Context& context, Term *element_type, std::size_t n_elements, Term *const* elements);


      virtual Term* type(Context *context, std::size_t n_parameters, Term *const* parameters) const;

    private:
      virtual bool equals_internal(const ProtoTerm& other) const = 0;
      virtual std::size_t hash_internal() const = 0;
      virtual ProtoTerm* clone() const = 0;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, Term*) const;
      virtual LLVMValueBuilder::Constant llvm_value_constant(LLVMValueBuilder&, Term*) const;
    };

    class StructType : public DerivedType {
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

    class UnionType : public DerivedType {
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
