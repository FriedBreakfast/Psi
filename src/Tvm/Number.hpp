#ifndef HPP_PSI_TVM_NUMBER
#define HPP_PSI_TVM_NUMBER

#include "Functional.hpp"
#include "BigInteger.hpp"

/**
 * \file
 *
 * Definitions for numeric types and operations, including BooleanType
 * and BooleanValue.
 */

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(BooleanType)

    PSI_TVM_FUNCTIONAL_TYPE(BooleanValue, ConstructorOperation)
    typedef PrimitiveWrapper<bool> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of this constant.
    bool value() const {return data().value();}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, bool);
    PSI_TVM_FUNCTIONAL_TYPE_END(BooleanValue)

    PSI_TVM_FUNCTIONAL_TYPE(IntegerType, TypeOperation)
    /// \brief Available integer bit widths
    enum Width {
      i8, ///< 8 bits
      i16, ///< 16 bits
      i32, ///< 32 bits
      i64, ///< 64 bits
      i128, ///< 128 bits
      /// Same width as a pointer. For platform independence, this
      /// is not considered equal to any of the other bit widths,
      /// even though in practise it will always be the same as one
      /// of them.
      iptr
    };

    struct Data {
      Data(Width width_, bool is_signed_)
        : width(width_), is_signed(is_signed_) {}

      Width width;
      bool is_signed;

      bool operator == (const Data& other) const {
        return (width == other.width) && (is_signed == other.is_signed);
      }

      friend std::size_t hash_value(const Data& self) {
        std::size_t h = 0;
        boost::hash_combine(h, int(self.width));
        boost::hash_combine(h, self.is_signed);
        return h;
      }
    };

    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the width of this integer type.
    Width width() const {return data().width;}
    /// \brief Whether this integer type is signed.
    bool is_signed() const {return data().is_signed;}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static unsigned value_bits(IntegerType::Width);
    static Ptr get(Context&, Width, bool);
    PSI_TVM_FUNCTIONAL_TYPE_END(IntegerType)

    PSI_TVM_FUNCTIONAL_TYPE(IntegerValue, ConstructorOperation)
    struct Data {
      IntegerType::Width width;
      bool is_signed;
      BigInteger value;
      
      bool operator == (const Data& other) const {
        return (width == other.width) && (is_signed == other.is_signed) && (value == other.value);
      }

      friend std::size_t hash_value(const Data& self) {
        std::size_t h = 0;
        boost::hash_combine(h, self.width);
        boost::hash_combine(h, self.is_signed);
        boost::hash_combine(h, self.value);
        return h;
      }
    };
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of this constant.
    const BigInteger& value() const {return data().value;}
    /// \brief Get the type of this term cast to IntegerType::Ptr
    IntegerType::Ptr type() const {return cast<IntegerType>(PtrBaseType::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, IntegerType::Width, bool, const BigInteger&);
    PSI_TVM_FUNCTIONAL_TYPE_END(IntegerValue)

    PSI_TVM_FUNCTIONAL_TYPE(FloatType, TypeOperation)
    enum Width {
      fp32, ///< 32-bit IEEE float
      fp64, ///< 64-bit IEEE float
      fp128, ///< 128-bit IEEE float
      fp_x86_80,  ///< 80-bit x86 "long double" type
      fp_ppc_128, ///< 128-bit PPC-specific floating point type. See
                  ///"man float" on MacOS for details.
    };

    typedef PrimitiveWrapper<Width> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the width of this floating point type.
    Width width() const {return data().value();}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static FloatType::Ptr get(Context&, Width width);
    PSI_TVM_FUNCTIONAL_TYPE_END(FloatType)

    inline std::size_t hash_value(FloatType::Width w) {
      return boost::hash_value(int(w));
    }

    PSI_TVM_FUNCTIONAL_TYPE(FloatValue, ConstructorOperation)
    struct Data {
      FloatType::Width width;
      unsigned exponent;
      char mantissa[16];

      bool operator == (const Data& other) const {
        return (exponent == other.exponent) && std::equal(mantissa, mantissa+sizeof(mantissa), other.mantissa);
      }

      friend std::size_t hash_value(const Data& self) {
        std::size_t h = 0;
        boost::hash_combine(h, self.exponent);
        boost::hash_range(h, self.mantissa, self.mantissa+sizeof(self.mantissa));
        return h;
      }
    };
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the type of this term cast to FloatType::Ptr
    FloatType::Ptr type() const {return cast<FloatType>(PtrBaseType::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static FloatValue::Ptr get(Context&, FloatType::Width width, const Data&);
    PSI_TVM_FUNCTIONAL_TYPE_END(FloatValue)

    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerAdd, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerMultiply, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerDivide, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_UNARY(IntegerNegative, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(BitAnd, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(BitOr, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(BitXor, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_UNARY(BitNot, IntegerType)
    
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerCompareEq, BooleanType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerCompareNe, BooleanType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerCompareGt, BooleanType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerCompareGe, BooleanType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerCompareLt, BooleanType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerCompareLe, BooleanType)

    /**
     * Select a value based on a boolean condition.
     */
    PSI_TVM_FUNCTIONAL_TYPE(SelectValue, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the condition which selects which value is returned.
    Term *condition() const {return get()->parameter(0);}
    /// \brief Get the value of this term if \c condition is true.
    Term *true_value() const {return get()->parameter(1);}
    /// \brief Get the value of this term if \c condition is false.
    Term *false_value() const {return get()->parameter(2);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static SelectValue::Ptr get(Term *condition, Term *true_value, Term *false_value);
    PSI_TVM_FUNCTIONAL_TYPE_END(SelectValue)
  }
}

#endif
