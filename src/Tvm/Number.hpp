#ifndef HPP_PSI_TVM_NUMBER
#define HPP_PSI_TVM_NUMBER

#include "Core.hpp"

#include <boost/type_traits/alignment_of.hpp>
#include <gmpxx.h>

namespace Psi {
  namespace Tvm {
    class PrimitiveTypeBase : public FunctionalTermBackend {
    public:
      virtual TermPtr<> type(Context& context, std::size_t n_parameters, Term *const* parameters) const;
      virtual LLVMFunctionBuilder::Result llvm_value_instruction(LLVMFunctionBuilder&, FunctionalTerm*) const;
      virtual LLVMConstantBuilder::Constant llvm_value_constant(LLVMConstantBuilder&, FunctionalTerm*) const;
    };

    template<typename T>
    class PrimitiveType : public PrimitiveTypeBase {
    public:
      typedef T ImplementingType;
      typedef PrimitiveType<ImplementingType> ThisType;

      PrimitiveType(ImplementingType impl) : m_impl(impl) {}

      virtual std::pair<std::size_t, std::size_t> size_align() const {
	return std::make_pair(sizeof(ThisType), boost::alignment_of<ThisType>::value);
     }

      virtual bool equals(const FunctionalTermBackend& other) const {
	return m_impl == *boost::polymorphic_downcast<ThisType*>(other).m_impl;
      }

      virtual FunctionalTermBackend* clone(void *dest) const {
	new (dest) PrimitiveType<T>(*this);
      }

      virtual LLVMConstantBuilder::Type llvm_type(LLVMConstantBuilder& builder, FunctionalTerm*) const {
	return LLVMConstantBuilder::type_known(m_impl.llvm_type(builder.context()));
      }

    private:
      virtual std::size_t hash_internal() const {
	return m_impl.hash_value();
      }

      ImplementingType m_impl;
    };

    class IntegerType {
    public:
      IntegerType(bool is_signed, unsigned n_bits);

      llvm::Type* llvm_type(llvm::LLVMContext&);
      bool operator == (const IntegerType&) const;
      std::size_t hash_value() const;

      static llvm::APInt mpl_to_llvm(bool is_signed, unsigned n_bits, const mpz_class& value);

    private:
      bool m_is_signed;
      unsigned m_n_bits;
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
