#ifndef HPP_PSI_TVM_NUMBER
#define HPP_PSI_TVM_NUMBER

#include "Core.hpp"
#include "Primitive.hpp"
#include "BigInteger.hpp"

#include <gmpxx.h>

namespace Psi {
  namespace Tvm {
    class BooleanType : public PrimitiveType, public StatelessTerm {
    public:
      virtual const llvm::Type* llvm_primitive_type(LLVMConstantBuilder&) const;
      typedef TrivialAccess<BooleanType> Access;
    };

    class ConstantBoolean : public PrimitiveValue {
    public:
      ConstantBoolean(bool value);

      bool operator == (const ConstantBoolean&) const;
      friend std::size_t hash_value(const ConstantBoolean&);
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      virtual llvm::Constant* llvm_primitive_value(LLVMConstantBuilder&) const;

      class Access {
      public:
	Access(const FunctionalTerm*, const ConstantBoolean *self) : m_self(self) {}
	/// \brief Get the constant value of this term
	bool value() const {return m_self->m_value;}
      private:
	const ConstantBoolean *m_self;
      };

    private:
      bool m_value;
    };

    class IntegerType : public PrimitiveType {
      friend class ConstantInteger;

    public:
      IntegerType(bool is_signed, unsigned n_bits);

      virtual const llvm::Type* llvm_primitive_type(LLVMConstantBuilder&) const;
      bool operator == (const IntegerType&) const;
      friend std::size_t hash_value(const IntegerType&);

      class Access {
      public:
	Access(const FunctionalTerm*, const IntegerType* self) : m_self(self) {}
	/// \brief Whether this type is signed
	bool is_signed() const {return m_self->m_is_signed;}
	/// \brief Number of bits in this type
	unsigned n_bits() const {return m_self->m_n_bits;}
      private:
	const IntegerType *m_self;
      };

    private:
      bool m_is_signed;
      unsigned m_n_bits;
    };

    class ConstantInteger : public PrimitiveValue {
    public:
      ConstantInteger(const IntegerType& type, const BigInteger& value);

      bool operator == (const ConstantInteger&) const;
      friend std::size_t hash_value(const ConstantInteger&);
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      virtual llvm::Constant* llvm_primitive_value(LLVMConstantBuilder&) const;

      class Access {
      public:
	Access(const FunctionalTerm*, const ConstantInteger *self) : m_self(self) {}
	/// \brief Get information about the type of this constant
	IntegerType::Access type() const {return IntegerType::Access(0, &m_self->m_type);}
	/// \brief Get the value of this constant
	const BigInteger& value() const {return m_self->m_value;}
      private:
	const ConstantInteger *m_self;
      };

    private:
      IntegerType m_type;
      BigInteger m_value;
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

    class RealType : public PrimitiveType {
    public:
      enum Width {
        real_float,
        real_double
      };

      RealType(Width width);

      virtual const llvm::Type* llvm_primitive_type(LLVMConstantBuilder&) const;
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

      class Access {
      public:
	Access(const FunctionalTerm*, const RealType *self) : m_self(self) {}
	/// \brief Get the width of this type
	Width width() const {return m_self->m_width;}
      private:
	const RealType *m_self;
      };

    private:
      Width m_width;
    };

    class ConstantReal : public PrimitiveValue {
    public:
      ConstantReal(const RealType& type, const mpf_class& value);

      bool operator == (const ConstantReal&) const;
      friend std::size_t hash_value(const ConstantReal&);
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      virtual llvm::Constant* llvm_primitive_value(LLVMConstantBuilder&) const;

      class Access {
      public:
	Access(const FunctionalTerm*, const ConstantReal* self) : m_self(self) {}
	/// \brief Get the type of this constant
	RealType::Access type() const {return RealType::Access(0, &m_self->m_type);}
	/// \brief Get the value of this constant
	const mpf_class& value() const {return m_self->m_value;}
      private:
	const ConstantReal *m_self;
      };

    private:
      RealType m_type;
      mpf_class m_value;
    };

    /**
     * \brief Categories of special floating point value.
     */
    class SpecialRealValue : public PrimitiveValue {
    public:
      SpecialRealValue(const RealType& type, SpecialReal value, bool negative=false);

      bool operator == (const SpecialRealValue&) const;
      friend std::size_t hash_value(const SpecialRealValue&);
      FunctionalTypeResult type(Context& context, ArrayPtr<Term*const> parameters) const;
      virtual llvm::Constant* llvm_primitive_value(LLVMConstantBuilder&) const;

      class Access {
      public:
	Access(const FunctionalTerm*, const SpecialRealValue* self) : m_self(self) {}
	/// \brief Get the type of this constant
	RealType::Access type() const {return RealType::Access(0, &m_self->m_type);}
	/// \brief Get the value (category of special value) of this constant
	SpecialReal value() const {return m_self->m_value;}
	/// \brief Whether this constant is negative
	bool negative() const {return m_self->m_negative;}
      private:
	const SpecialRealValue *m_self;
      };

    private:
      RealType m_type;
      SpecialReal m_value;
      bool m_negative;
    };
  }
}

#endif
