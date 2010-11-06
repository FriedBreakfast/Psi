#ifndef HPP_PSI_TVM_NUMBER
#define HPP_PSI_TVM_NUMBER

#include "Core.hpp"
#include "Primitive.hpp"

#include <gmpxx.h>

namespace Psi {
  namespace Tvm {
    class IntegerType : public PrimitiveType<IntegerType> {
    public:
      IntegerType(bool is_signed, unsigned n_bits);

      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMType llvm_type(LLVMValueBuilder&) const;
      bool operator == (const IntegerType&) const;
      friend std::size_t hash_value(const IntegerType&);

      llvm::APInt mpl_to_llvm(const mpz_class& value) const;
      static llvm::APInt mpl_to_llvm(bool is_signed, unsigned n_bits, const mpz_class& value);

    private:
      bool m_is_signed;
      unsigned m_n_bits;
    };

    class ConstantInteger : public PrimitiveValue<ConstantInteger> {
    public:
      ConstantInteger(const IntegerType& type, const mpz_class& value);

      bool operator == (const ConstantInteger&) const;
      friend std::size_t hash_value(const ConstantInteger&);
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

    private:
      IntegerType m_type;
      mpz_class m_value;
    };

    enum SpecialReal {
      special_real_zero,
      special_real_nan,
      special_real_qnan,
      special_real_snan,
      special_real_largest,
      special_real_smallest,
      special_real_smallest_normalized
    };

    class RealType : public PrimitiveType<RealType> {
    public:
      enum Width {
        real_float,
        real_double
      };

      RealType(Width width);

      LLVMType llvm_type(LLVMValueBuilder&, Term&) const;
      LLVMType llvm_type(LLVMValueBuilder&) const;
      bool operator == (const RealType&) const;
      friend std::size_t hash_value(const RealType&);

      llvm::APFloat mpl_to_llvm(const mpf_class& value) const;
      llvm::APFloat special_to_llvm(SpecialReal which, bool negative) const;
      const llvm::fltSemantics& llvm_semantics() const;

      /**
       * \brief Convert an MPL real to an llvm::APFloat.
       */
      static llvm::APFloat mpl_to_llvm(const llvm::fltSemantics& semantics, const mpf_class& value);

      /**
       * \brief Get an llvm::APFloat for a special value (see #Value).
       */
      static llvm::APFloat special_to_llvm(const llvm::fltSemantics& semantics, SpecialReal which, bool negative);

    private:
      Width m_width;
    };

    class ConstantReal : public PrimitiveValue<ConstantReal> {
    public:
      ConstantReal(const RealType& type, const mpf_class& value);

      bool operator == (const ConstantReal&) const;
      friend std::size_t hash_value(const ConstantReal&);
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

    private:
      RealType m_type;
      mpf_class m_value;
    };

    /**
     * \brief Categories of special floating point value.
     */
    class SpecialRealValue : public PrimitiveValue<SpecialRealValue> {
    public:
      SpecialRealValue(const RealType& type, SpecialReal value, bool negative=false);

      bool operator == (const SpecialRealValue&) const;
      friend std::size_t hash_value(const SpecialRealValue&);
      TermPtr<> type(Context& context, TermRefArray<> parameters) const;
      LLVMValue llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm& term) const;

    private:
      RealType m_type;
      SpecialReal m_value;
      bool m_negative;
    };
  }
}

#endif
