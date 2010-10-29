#include "Number.hpp"
#include "../Utility.hpp"

#include <sstream>

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>

namespace Psi {
  namespace Tvm {
    IntegerType::IntegerType(bool is_signed, unsigned n_bits)
      : m_is_signed(is_signed), m_n_bits(n_bits) {
    }

    LLVMType IntegerType::llvm_type(LLVMValueBuilder& builder) const {
      const llvm::Type *ty = llvm::IntegerType::get(builder.context(), m_n_bits);
      return LLVMType::known(const_cast<llvm::Type*>(ty));
    }

    LLVMType IntegerType::llvm_type(LLVMValueBuilder& builder, Term&) const {
      return llvm_type(builder);
    }

    bool IntegerType::operator == (const IntegerType& o) const {
      return (m_is_signed == o.m_is_signed) &&
	(m_n_bits == o.m_n_bits);
    }

    std::size_t hash_value(const IntegerType& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.m_is_signed);
      boost::hash_combine(h, self.m_n_bits);
      return h;
    }

    llvm::APInt IntegerType::mpl_to_llvm(const mpz_class& value) const {
      return mpl_to_llvm(m_is_signed, m_n_bits, value);
    }

    llvm::APInt IntegerType::mpl_to_llvm(bool is_signed, unsigned n_bits, const mpz_class& value) {
      std::size_t value_bits = mpz_sizeinbase(value.get_mpz_t(), 2);
      if (mpz_sgn(value.get_mpz_t()) < 0) {
	if (!is_signed)
	  throw std::logic_error("integer literal value of out range");
	value_bits++;
      }
      value_bits = std::max(value_bits, std::size_t(n_bits));

      std::string text = value.get_str(16);
      llvm::APInt ap(value_bits, text, 16);

      if (n_bits == value_bits)
	return ap;

      if (is_signed) {
	if (ap.isSignedIntN(n_bits))
	  return ap.sext(n_bits);
	else
	  throw std::logic_error("integer literal value of out range");
      } else {
	if (ap.isIntN(n_bits))
	  return ap.zext(n_bits);
	else
	  throw std::logic_error("integer literal value of out range");
      }
    }

    ConstantInteger::ConstantInteger(const IntegerType& type, const mpz_class& value)
      : m_type(type), m_value(value) {
    }

    bool ConstantInteger::operator == (const ConstantInteger& o) const {
      return (m_type == o.m_type) && (m_value == o.m_value);
    }

    std::size_t hash_value(const ConstantInteger& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.m_type);
      boost::hash_combine(h, self.m_value.get_ui());
      return h;
    }

    TermPtr<> ConstantInteger::type(Context& context, std::size_t n_parameters, Term *const* parameters) const {
      check_primitive_parameters(n_parameters, parameters);
      return context.get_functional_v(m_type);
    }

    LLVMValue ConstantInteger::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm&) const {
      LLVMType ty = m_type.llvm_type(builder);
      PSI_ASSERT(ty.is_valid());
      llvm::APInt llvm_value = m_type.mpl_to_llvm(m_value);
      return LLVMValue::known(llvm::ConstantInt::get(ty.type(), llvm_value));
    }

    RealType::RealType(Width width) : m_width(width) {
    }

    LLVMType RealType::llvm_type(LLVMValueBuilder& builder) const {
      const llvm::Type *ty;
      switch (m_width) {
      case real_float: ty = llvm::Type::getFloatTy(builder.context()); break;
      case real_double: ty = llvm::Type::getDoubleTy(builder.context()); break;

      default:
        PSI_FAIL("unknown real width");
      }

      return LLVMType::known(const_cast<llvm::Type*>(ty));
    }

    LLVMType RealType::llvm_type(LLVMValueBuilder& builder, Term&) const {
      return llvm_type(builder);
    }

    bool RealType::operator == (const RealType& o) const {
      return m_width == o.m_width;
    }

    std::size_t hash_value(const RealType& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.m_width);
      return h;
    }

    const llvm::fltSemantics& RealType::llvm_semantics() const {
      switch(m_width) {
      case real_float: return llvm::APFloat::IEEEsingle;
      case real_double: return llvm::APFloat::IEEEdouble;

      default:
        PSI_FAIL("unknown real width");
      }
    }

    llvm::APFloat RealType::mpl_to_llvm(const mpf_class& value) const {
      return mpl_to_llvm(llvm_semantics(), value);
    }

    llvm::APFloat RealType::special_to_llvm(SpecialReal which, bool negative) const {
      return special_to_llvm(llvm_semantics(), which, negative);
    }

    llvm::APFloat RealType::mpl_to_llvm(const llvm::fltSemantics& semantics, const mpf_class& value) {
      mp_exp_t exp;

      std::stringstream r;
      if (mpf_sgn(value.get_mpf_t()) < 0)
	r << "-";

      r << "0." << value.get_str(exp);
      r << "e" << exp;

      return llvm::APFloat(semantics, r.str());
    }

    llvm::APFloat RealType::special_to_llvm(const llvm::fltSemantics& semantics, SpecialReal v, bool negative) {
      switch (v) {
      case special_real_zero: return llvm::APFloat::getZero(semantics, negative);
      case special_real_nan: return llvm::APFloat::getNaN(semantics, negative);
      case special_real_qnan: return llvm::APFloat::getQNaN(semantics, negative);
      case special_real_snan: return llvm::APFloat::getSNaN(semantics, negative);
      case special_real_largest: return llvm::APFloat::getLargest(semantics, negative);
      case special_real_smallest: return llvm::APFloat::getSmallest(semantics, negative);
      case special_real_smallest_normalized: return llvm::APFloat::getSmallestNormalized(semantics, negative);

      default:
	PSI_FAIL("unknown special floating point value");
      }
    }

    ConstantReal::ConstantReal(const RealType& type, const mpf_class& value)
      : m_type(type), m_value(value) {
    }

    bool ConstantReal::operator == (const ConstantReal& o) const {
      return (m_type == o.m_type) && (m_value == o.m_value);
    }

    std::size_t hash_value(const ConstantReal& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.m_type);
      boost::hash_combine(h, self.m_value.get_d());
      return h;
    }

    TermPtr<> ConstantReal::type(Context& context, std::size_t n_parameters, Term *const* parameters) const {
      check_primitive_parameters(n_parameters, parameters);
      return context.get_functional_v(m_type);
    }

    LLVMValue ConstantReal::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm&) const {
      llvm::APFloat llvm_value = m_type.mpl_to_llvm(m_value);
      return LLVMValue::known(llvm::ConstantFP::get(builder.context(), llvm_value));
    }

    SpecialRealValue::SpecialRealValue(const RealType& type, SpecialReal value, bool negative)
      : m_type(type), m_value(value), m_negative(negative) {
    }

    bool SpecialRealValue::operator == (const SpecialRealValue& o) const {
      return (m_type == o.m_type) &&
        (m_value == o.m_value) &&
        (m_negative == o.m_negative);
    }

    std::size_t hash_value(const SpecialRealValue& self) {
      std::size_t h = 0;
      boost::hash_combine(h, self.m_type);
      boost::hash_combine(h, self.m_value);
      boost::hash_combine(h, self.m_negative);
      return h;
    }

    TermPtr<> SpecialRealValue::type(Context& context, std::size_t n_parameters, Term *const* parameters) const {
      check_primitive_parameters(n_parameters, parameters);
      return context.get_functional_v(m_type);
    }

    LLVMValue SpecialRealValue::llvm_value_constant(LLVMValueBuilder& builder, FunctionalTerm&) const {
      llvm::APFloat llvm_value = m_type.special_to_llvm(m_value, m_negative);
      return LLVMValue::known(llvm::ConstantFP::get(builder.context(), llvm_value));
    }
  }
}
