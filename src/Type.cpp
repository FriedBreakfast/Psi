#include "Type.hpp"

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>

#include <sstream>

namespace Psi {
  TemplateType::TemplateType(std::size_t n_parameters)
    : m_n_parameters(n_parameters) {
  }

  TemplateType::~TemplateType() {
  }

  bool AppliedType::is_aggregate() {
    return dynamic_cast<AggregateType*>(template_());
  }

  void OpaqueType::unify(TemplateType *ty) {
    assert(n_parameters() == ty->n_parameters());
    replace_with(ty);
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

  llvm::Value* IntegerType::constant_to_llvm(llvm::LLVMContext& context, const mpz_class& value) {
    const llvm::Type *ty = llvm::IntegerType::get(context, m_n_bits);
    return llvm::ConstantInt::get(ty, mpl_to_llvm(m_is_signed, m_n_bits, value));
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
    case SpecialReal::zero: return llvm::APFloat::getZero(semantics, negative);
    case SpecialReal::nan: return llvm::APFloat::getNaN(semantics, negative);
    case SpecialReal::qnan: return llvm::APFloat::getQNaN(semantics, negative);
    case SpecialReal::snan: return llvm::APFloat::getSNaN(semantics, negative);
    case SpecialReal::largest: return llvm::APFloat::getLargest(semantics, negative);
    case SpecialReal::smallest: return llvm::APFloat::getSmallest(semantics, negative);
    case SpecialReal::smallest_normalized: return llvm::APFloat::getSmallestNormalized(semantics, negative);

    default:
      throw std::logic_error("unknown special floating point value");
    }
  }
}
