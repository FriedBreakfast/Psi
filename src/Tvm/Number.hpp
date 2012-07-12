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
    class BooleanType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(BooleanType)
      
    public:
      BooleanType(Context& context, const SourceLocation& location);
      static ValuePtr<BooleanType> get(Context& context, const SourceLocation& location);
    };

    class BooleanValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(BooleanValue)
      
    private:
      bool m_value;
      
    public:
      /// \brief Get the value of this constant.
      bool value() const {return m_value;}

      static ValuePtr<BooleanValue> get(Context& context, bool value, const SourceLocation& location);
    };

    class IntegerType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(IntegerType)
      
    public:
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
      
      Width width() const {return m_width;}
      bool is_signed() const {return m_is_signed;}
      unsigned bits() const {return value_bits(m_width);}
      
      static ValuePtr<IntegerType> get(Context& context, Width width, bool is_signed, const SourceLocation& location);
      static ValuePtr<IntegerType> get_intptr(Context& context, const SourceLocation& location);

      static unsigned value_bits(IntegerType::Width width);
      
    private:
      Width m_width;
      bool m_is_signed;
    };

    class IntegerValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(IntegerValue)
      
    private:
      IntegerType::Width m_width;
      bool m_is_signed;
      BigInteger m_value;
      
    public:
      /// \brief Get the value of this constant.
      const BigInteger& value() const {return m_value;}
      /// \brief Get the type of this term cast to IntegerType::Ptr
      ValuePtr<IntegerType> type() const {return value_cast<IntegerType>(Value::type());}

      static ValuePtr<IntegerValue> get(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location);
      static ValuePtr<> get_intptr(Context& context, std::size_t n, const SourceLocation& location);
    };

    class FloatType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(FloatType)

    public:
      enum Width {
        fp32, ///< 32-bit IEEE float
        fp64, ///< 64-bit IEEE float
        fp128, ///< 128-bit IEEE float
        fp_x86_80,  ///< 80-bit x86 "long double" type
        fp_ppc_128, ///< 128-bit PPC-specific floating point type. See
                    ///"man float" on MacOS for details.
      };

      /// \brief Get the width of this floating point type.
      Width width() const {return m_width;}

      static ValuePtr<FloatType> get(Context& context, Width width, const SourceLocation& location);
      
    private:
      Width m_width;
    };

    class FloatValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(FloatValue)

    public:
      static const unsigned mantissa_width = 16;

    private:
      FloatType::Width m_width;
      unsigned m_exponent;
      char m_mantissa[mantissa_width];
      
    public:
      /// \brief Get the type of this term cast to FloatType::Ptr
      ValuePtr<FloatType> type() const {return value_cast<FloatType>(Value::type());}

      static ValuePtr<FloatValue> get(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa);
    };
    
    /**
     * \brief Unary operations on integers.
     */
    class IntegerUnaryOp : public UnaryOp {
      PSI_TVM_FUNCTIONAL_DECL(IntegerUnaryOp)

    public:
      enum Which {
        int_neg,
        int_not
      };
      
      IntegerUnaryOp(Which which, const ValuePtr<>& arg);
      /// \brief Which opertion this is
      Which which() const {return m_which;}
      
    private:
      Which m_which;
    };

    PSI_TVM_UNARY_OP_DECL(IntegerNegative, IntegerUnaryOp)
    PSI_TVM_UNARY_OP_DECL(BitNot, IntegerUnaryOp)

    /**
     * \brief Binary operations on two integers of the same width.
     */
    class IntegerBinaryOp : public BinaryOp {
    public:
      enum Which {
        int_add,
        int_mul,
        int_div,
        int_and,
        int_or,
        int_xor
      };
      
      /// \brief Which opertion this is
      Which which() const {return m_which;}

    protected:
      IntegerBinaryOp(Which which, const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      
    private:
      Which m_which;
    };
    
    
    PSI_TVM_BINARY_OP_DECL(IntegerAdd, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerMultiply, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerDivide, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitAnd, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitOr, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitXor, IntegerBinaryOp);
    
    PSI_TVM_BINARY_OP_DECL(IntegerCompareEq, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareNe, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareGt, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareGe, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareLt, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareLe, IntegerBinaryOp);
    
    class Select : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(Select)

    public:
      /// \brief Get the condition which selects which value is returned.
      const ValuePtr<>& condition() const {return m_condition;}
      /// \brief Get the value of this term if \c condition is true.
      const ValuePtr<>& true_value() const {return m_true_value;}
      /// \brief Get the value of this term if \c condition is false.
      const ValuePtr<>& false_value() const {return m_false_value;}

      static ValuePtr<Select> get(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location);

    private:
      ValuePtr<> m_condition;
      ValuePtr<> m_true_value;
      ValuePtr<> m_false_value;
    };
  }
}

#endif
