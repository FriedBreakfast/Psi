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
    class BooleanType : public SimpleType {
      PSI_TVM_FUNCTIONAL_DECL(BooleanType)
      
    public:
      BooleanType(Context& context, const SourceLocation& location);
      static ValuePtr<BooleanType> get(Context& context, const SourceLocation& location);
    };

    class BooleanValue : public SimpleConstructor {
      PSI_TVM_FUNCTIONAL_DECL(BooleanValue)
      
    private:
      bool m_value;
      
    public:
      BooleanValue(Context& context, bool value, const SourceLocation& location);
      /// \brief Get the value of this constant.
      bool value() const {return m_value;}

      static ValuePtr<BooleanValue> get(Context& context, bool value, const SourceLocation& location);
    };

    class IntegerType : public SimpleType {
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
      
      IntegerType(Context& context, Width width, bool is_signed, const SourceLocation& location);
      static ValuePtr<IntegerType> get(Context& context, Width width, bool is_signed, const SourceLocation& location);
      static ValuePtr<IntegerType> get_intptr(Context& context, const SourceLocation& location);

      static unsigned value_bits(IntegerType::Width width);
      
    private:
      Width m_width;
      bool m_is_signed;
    };

    class IntegerValue : public SimpleConstructor {
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

      IntegerValue(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location);
      static ValuePtr<IntegerValue> get(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location);
      static ValuePtr<IntegerValue> get_intptr(Context& context, unsigned n, const SourceLocation& location);
    };

    class FloatType : public SimpleType {
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

      FloatType(Context& context, Width width, const SourceLocation& location);
      static ValuePtr<FloatType> get(Context& context, Width width, const SourceLocation& location);
      
    private:
      Width m_width;
    };

    class FloatValue : public SimpleConstructor {
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

      FloatValue(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location);
      static ValuePtr<FloatValue> get(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location);
    };
    
    /**
     * \brief Unary operations on integers.
     */
    class IntegerUnaryOp : public UnaryOp {
    protected:
      IntegerUnaryOp(const ValuePtr<>& arg, const HashableValueSetup& setup, const SourceLocation& location);
    };

    PSI_TVM_UNARY_OP_DECL(IntegerNegative, IntegerUnaryOp)
    PSI_TVM_UNARY_OP_DECL(BitNot, IntegerUnaryOp)

    /**
     * \brief Binary operations on two integers of the same width.
     */
    class IntegerBinaryOp : public BinaryOp {
    protected:
      IntegerBinaryOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const HashableValueSetup& setup, const SourceLocation& location);
    };
    
    
    PSI_TVM_BINARY_OP_DECL(IntegerAdd, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerMultiply, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerDivide, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitAnd, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitOr, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitXor, IntegerBinaryOp);
    
    class IntegerCompareOp : public BinaryOp {
    protected:
      IntegerCompareOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const HashableValueSetup& setup, const SourceLocation& location);
    };
    
    PSI_TVM_BINARY_OP_DECL(IntegerCompareEq, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareNe, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareGt, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareGe, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareLt, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareLe, IntegerCompareOp);
    
    class Select : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(Select)

    public:
      /// \brief Get the condition which selects which value is returned.
      const ValuePtr<>& condition() const {return m_condition;}
      /// \brief Get the value of this term if \c condition is true.
      const ValuePtr<>& true_value() const {return m_true_value;}
      /// \brief Get the value of this term if \c condition is false.
      const ValuePtr<>& false_value() const {return m_false_value;}

      Select(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location);
      static ValuePtr<Select> get(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location);

    private:
      ValuePtr<> m_condition;
      ValuePtr<> m_true_value;
      ValuePtr<> m_false_value;
    };
  }
}

#endif
