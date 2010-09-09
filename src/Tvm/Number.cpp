#include "Number.hpp"
#include "../Utility.hpp"

#include <sstream>

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>

namespace Psi {
  namespace Tvm {
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

    BasicIntegerType::BasicIntegerType(unsigned n_bits, bool is_signed)
      : m_n_bits(n_bits), m_is_signed(is_signed) {
    }

#define PSI_TVM_INT(bits)						\
    BasicIntegerType BasicIntegerType::int##bits() {return BasicIntegerType(bits, true);} \
    BasicIntegerType BasicIntegerType::uint##bits() {return BasicIntegerType(bits, false);} \
    Term* BasicIntegerType::int##bits##_term(Context& context) {return context.new_term(int##bits());} \
    Term* BasicIntegerType::uint##bits##_term(Context& context) {return context.new_term(uint##bits());}

    PSI_TVM_INT(8)
    PSI_TVM_INT(16)
    PSI_TVM_INT(32)
    PSI_TVM_INT(64)

#undef PSI_TVM_INT

    llvm::Constant* BasicIntegerType::constant_to_llvm(llvm::LLVMContext& context, const mpz_class& value) {
      const llvm::Type *ty = llvm::IntegerType::get(context, m_n_bits);
      return llvm::ConstantInt::get(ty, mpl_to_llvm(m_is_signed, m_n_bits, value));
    }

    bool BasicIntegerType::equals_internal(const ProtoTerm& other) const {
      const BasicIntegerType& o = static_cast<const BasicIntegerType&>(other);
      return (m_n_bits == o.m_n_bits) && (m_is_signed == o.m_is_signed);
    }
    
    std::size_t BasicIntegerType::hash_internal() const {
      return HashCombiner() << m_n_bits << m_is_signed;
    }

    ProtoTerm* BasicIntegerType::clone() const {
      return new BasicIntegerType(*this);
    }

    LLVMConstantBuilder::Constant BasicIntegerType::llvm_value_constant(LLVMConstantBuilder& builder, Term*) const {
      return Metatype::llvm_value(llvm::IntegerType::get(builder.context(), m_n_bits));
    }

    LLVMConstantBuilder::Type BasicIntegerType::llvm_type(LLVMConstantBuilder& builder, Term*) const {
      return LLVMConstantBuilder::type_known(const_cast<llvm::IntegerType*>(llvm::IntegerType::get(builder.context(), m_n_bits)));
    }

    Term* ConstantInteger::create(Term *type, const mpz_class& value) {
      return type->context().new_term(ConstantInteger(value), type);
    }

    ConstantInteger::ConstantInteger(const mpz_class& value)
      : m_value(value) {
    }

    Term* ConstantInteger::type(Context&, std::size_t n_parameters, Term *const* parameters) const {
      if (n_parameters != 1)
	throw std::logic_error("ConstantReal expects one parameter");

      if (!dynamic_cast<IntegerType*>(&parameters[0]->proto()))
	throw std::logic_error("ConstantReal parameter must be of type RealType");

      return parameters[0];
    }

    bool ConstantInteger::equals_internal(const ProtoTerm& other) const {
      const ConstantInteger& o = static_cast<const ConstantInteger&>(other);
      return m_value == o.m_value;
    }

    std::size_t ConstantInteger::hash_internal() const {
      return m_value.get_ui();
    }

    ProtoTerm* ConstantInteger::clone() const {
      return new ConstantInteger(*this);
    }

    LLVMConstantBuilder::Constant ConstantInteger::llvm_value_constant(LLVMConstantBuilder& builder, Term* term) const {
      IntegerType& type_proto = checked_reference_static_cast<IntegerType>(term->type()->proto());
      return LLVMConstantBuilder::constant_value(type_proto.constant_to_llvm(builder.context(), m_value));
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

    ConstantReal::ConstantReal(const mpf_class& value)
      : m_value(value) {
    }

    Term* ConstantReal::create(Term *type, const mpf_class& value) {
      return type->context().new_term(ConstantReal(value), type);
    }

    Term* ConstantReal::type(Context&, std::size_t n_parameters, Term *const* parameters) const {
      if (n_parameters != 1)
	throw std::logic_error("ConstantReal expects one parameter");

      if (!dynamic_cast<RealType*>(&parameters[0]->proto()))
	throw std::logic_error("ConstantReal parameter must be of type RealType");

      return parameters[0];
    }

    bool ConstantReal::equals_internal(const ProtoTerm& other) const {
      const ConstantReal& o = static_cast<const ConstantReal&>(other);
      return m_value == o.m_value;
    }

    std::size_t ConstantReal::hash_internal() const {
      return m_value.get_ui();
    }

    ProtoTerm* ConstantReal::clone() const {
      return new ConstantReal(*this);
    }

    LLVMConstantBuilder::Constant ConstantReal::llvm_value_constant(LLVMConstantBuilder& builder, Term* term) const {
      RealType& type_proto = checked_reference_static_cast<RealType>(term->type()->proto());
      return LLVMConstantBuilder::constant_value(type_proto.constant_to_llvm(builder.context(), m_value));
    }
  }
}
