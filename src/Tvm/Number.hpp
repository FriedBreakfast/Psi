#ifndef HPP_PSI_TVM_NUMBER
#define HPP_PSI_TVM_NUMBER

#include "Core.hpp"

#include <gmpxx.h>

namespace Psi {
  namespace Tvm {
    class IntegerType : public PrimitiveType {
    public:
      static llvm::APInt mpl_to_llvm(bool is_signed, unsigned n_bits, const mpz_class& value);

      virtual llvm::Constant* constant_to_llvm(llvm::LLVMContext& context, const mpz_class& value) = 0;
    };

    class BasicIntegerType : public IntegerType {
    public:
#define PSI_TVM_INT(bits)				\
      static BasicIntegerType int##bits();		\
      static BasicIntegerType uint##bits();		\
      static Term* int##bits##_term(Context& context);	\
      static Term* uint##bits##_term(Context& context);

      PSI_TVM_INT(8)
      PSI_TVM_INT(16)
      PSI_TVM_INT(32)
      PSI_TVM_INT(64)

#undef PSI_TVM_INT

      virtual llvm::Constant* constant_to_llvm(llvm::LLVMContext& context, const mpz_class& value);

    private:
      BasicIntegerType(unsigned n_bits, bool is_signed);

      virtual bool equals_internal(const ProtoTerm& other) const;
      virtual std::size_t hash_internal() const;
      virtual ProtoTerm* clone() const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const;
      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder&, Term*) const;
      virtual void validate_parameters(Context& context, std::size_t n_parameters, Term *const* parameters) const;

      unsigned m_n_bits;
      bool m_is_signed;
    };

    class ConstantInteger : public ConstantValue {
    public:
      ConstantInteger(const mpz_class& value);

      static Term* create(Term *type, const mpz_class& value);

      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const;

      const mpz_class& value() const {return m_value;}

    private:
      mpz_class m_value;

      virtual bool equals_internal(const ProtoTerm& other) const;
      virtual std::size_t hash_internal() const;
      virtual ProtoTerm* clone() const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const;
    };

    /**
     * \brief Categories of special floating point value.
     */
    class SpecialReal {
    public:
      enum Category {
	zero,
	nan,
	qnan,
	snan,
	largest,
	smallest,
	smallest_normalized
      };

      SpecialReal() {}
      SpecialReal(Category v) : m_v(v) {}
      /**
       * This is to enable \c switch functionality. #Category instances
       * should not be used directly.
       */
      operator Category () {return m_v;}

    private:
      Category m_v;
    };

    class RealType : public Type {
    public:
      /**
       * \brief Convert an MPL real to an llvm::APFloat.
       */
      static llvm::APFloat mpl_to_llvm(const llvm::fltSemantics& semantics, const mpf_class& value);
      /**
       * \brief Get an llvm::APFloat for a special value (see #Value).
       */
      static llvm::APFloat special_to_llvm(const llvm::fltSemantics& semantics, SpecialReal which, bool negative);

      virtual llvm::Constant* constant_to_llvm(llvm::LLVMContext& context, const mpf_class& value) = 0;
      virtual llvm::Constant* special_to_llvm(llvm::LLVMContext& context, SpecialReal which, bool negative=false) = 0;
    };

    class ConstantReal : public ConstantValue {
    public:
      ConstantReal(const mpf_class& value);
      static Term* create(Term *type, const mpf_class& value);

      virtual Term* type(Context& context, std::size_t n_parameters, Term *const* parameters) const;
      const mpf_class& value() {return m_value;}

    private:
      mpf_class m_value;

      virtual bool equals_internal(const ProtoTerm& other) const;
      virtual std::size_t hash_internal() const;
      virtual ProtoTerm* clone() const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, Term*) const;
    };
  }
}

#endif
