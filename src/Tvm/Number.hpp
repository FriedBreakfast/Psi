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
    class PSI_TVM_EXPORT BooleanType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(BooleanType)
      
    public:
      BooleanType(Context& context, const SourceLocation& location);
    };

    class PSI_TVM_EXPORT BooleanValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(BooleanValue)
      
    private:
      bool m_value;
      
    public:
      BooleanValue(Context& context, bool value, const SourceLocation& location);
      /// \brief Get the value of this constant.
      bool value() const {return m_value;}
    };

    class PSI_TVM_EXPORT IntegerType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(IntegerType)
      
    public:
      /// \brief Available integer bit widths
      enum Width {
        /// Same width as a pointer. For platform independence, this
        /// is not considered equal to any of the other bit widths,
        /// even though in practise it will always be the same as one
        /// of them.
        iptr=0,
        i8, ///< 8 bits
        i16, ///< 16 bits
        i32, ///< 32 bits
        i64, ///< 64 bits
        i128, ///< 128 bits
        i_max /// Number of integer types
      };
      
      Width width() const {return m_width;}
      bool is_signed() const {return m_is_signed;}
      unsigned bits() const {return value_bits(m_width);}
      
      IntegerType(Context& context, Width width, bool is_signed, const SourceLocation& location);

      static unsigned value_bits(IntegerType::Width width);
      static boost::optional<IntegerType::Width> width_from_bits(unsigned bits);
      
    private:
      Width m_width;
      bool m_is_signed;
    };

    PSI_VISIT_SIMPLE(IntegerType::Width);

    class PSI_TVM_EXPORT IntegerValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(IntegerValue)
      
    private:
      IntegerType::Width m_width;
      bool m_is_signed;
      BigInteger m_value;
      
    public:
      /// \brief Get the value of this constant.
      const BigInteger& value() const {return m_value;}
      /// \brief Get the width of this constant.
      IntegerType::Width width() const {return m_width;}
      /// \brief Whether this integer is signed.
      bool is_signed() const {return m_is_signed;}
      /// \brief Get the type of this term cast to IntegerType::Ptr
      ValuePtr<IntegerType> type() const {return value_cast<IntegerType>(Value::type());}

      IntegerValue(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location);
    };

    class PSI_TVM_EXPORT FloatType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(FloatType)

    public:
      enum Width {
        fp32=0, ///< 32-bit IEEE float
        fp64, ///< 64-bit IEEE float
        fp128, ///< 128-bit IEEE float
        fp_x86_80,  ///< 80-bit x86 "long double" type
        fp_ppc_128, ///< 128-bit PPC-specific floating point type. See
                    ///"man float" on MacOS for details.
        fp_max ///< Number of FP types; must be last in this structure
      };

      /// \brief Get the width of this floating point type.
      Width width() const {return m_width;}

      FloatType(Context& context, Width width, const SourceLocation& location);
      
    private:
      Width m_width;
    };
    
    PSI_VISIT_SIMPLE(FloatType::Width);

    class PSI_TVM_EXPORT FloatValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(FloatValue)

    public:
      static const unsigned mantissa_width = 16;

    private:
      FloatType::Width m_width;
      unsigned m_exponent;
      boost::array<char, mantissa_width> m_mantissa;
      
    public:
      /// \brief Get the type of this term cast to FloatType::Ptr
      ValuePtr<FloatType> type() const {return value_cast<FloatType>(Value::type());}

      FloatValue(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location);
    };
    
    /**
     * \brief Unary operations on integers.
     */
    class PSI_TVM_EXPORT IntegerUnaryOp : public UnaryOp {
    protected:
      IntegerUnaryOp(const ValuePtr<>& arg, const SourceLocation& location);
      virtual ValuePtr<> check_type() const;
    public:
      template<typename V> static void visit(V& v);
    };

    PSI_TVM_UNARY_OP_DECL(IntegerNegative, IntegerUnaryOp)
    PSI_TVM_UNARY_OP_DECL(BitNot, IntegerUnaryOp)

    /**
     * \brief Binary operations on two integers of the same width.
     */
    class IntegerBinaryOp : public BinaryOp {
    protected:
      IntegerBinaryOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      virtual ValuePtr<> check_type() const;
    public:
      template<typename V> static void visit(V& v);
    };
    
    PSI_TVM_BINARY_OP_DECL(IntegerAdd, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerMultiply, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(IntegerDivide, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitAnd, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitOr, IntegerBinaryOp);
    PSI_TVM_BINARY_OP_DECL(BitXor, IntegerBinaryOp);
    
    class PSI_TVM_EXPORT IntegerCompareOp : public BinaryOp {
    protected:
      IntegerCompareOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      virtual ValuePtr<> check_type() const;
    public:
      template<typename V> static void visit(V& v);
    };
    
    PSI_TVM_BINARY_OP_DECL(IntegerCompareEq, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareNe, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareGt, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareGe, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareLt, IntegerCompareOp);
    PSI_TVM_BINARY_OP_DECL(IntegerCompareLe, IntegerCompareOp);
    
    class PSI_TVM_EXPORT IntegerShiftOp : public BinaryOp {
    protected:
      IntegerShiftOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      virtual ValuePtr<> check_type() const;
    public:
      template<typename V> static void visit(V& v);
    };
    
    PSI_TVM_BINARY_OP_DECL(ShiftLeft, IntegerShiftOp);
    PSI_TVM_BINARY_OP_DECL(ShiftRight, IntegerShiftOp);
    
    class PSI_TVM_EXPORT BitCast : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(BitCast)
      
    public:
      /// \brief Get the value being cast
      const ValuePtr<>& value() const {return m_value;}
      /// \brief Get the target type of the cast
      const ValuePtr<>& target_type() const {return m_target_type;}
      
      BitCast(const ValuePtr<>& value, const ValuePtr<>& target_type, const SourceLocation& location);
      
    private:
      ValuePtr<> m_value;
      ValuePtr<> m_target_type;
    };
    
    class PSI_TVM_EXPORT Select : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(Select)

    public:
      /// \brief Get the condition which selects which value is returned.
      const ValuePtr<>& condition() const {return m_condition;}
      /// \brief Get the value of this term if \c condition is true.
      const ValuePtr<>& true_value() const {return m_true_value;}
      /// \brief Get the value of this term if \c condition is false.
      const ValuePtr<>& false_value() const {return m_false_value;}

      Select(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location);

    private:
      ValuePtr<> m_condition;
      ValuePtr<> m_true_value;
      ValuePtr<> m_false_value;
    };
    
    unsigned size_to_unsigned(const ValuePtr<>& value);
    bool size_equals_constant(const ValuePtr<>& value, unsigned c);
  }
}

#endif
