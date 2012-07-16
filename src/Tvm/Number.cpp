#include "Aggregate.hpp"
#include "FunctionalBuilder.hpp"
#include "Number.hpp"

#include <limits>

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_IMPL(BooleanType, SimpleType, bool)
    PSI_TVM_FUNCTIONAL_IMPL(BooleanValue, SimpleConstructor, bool_v)
    PSI_TVM_FUNCTIONAL_IMPL(IntegerType, SimpleType, int)
    PSI_TVM_FUNCTIONAL_IMPL(IntegerValue, SimpleConstructor, int_v)
    PSI_TVM_FUNCTIONAL_IMPL(FloatType, SimpleType, float)
    PSI_TVM_FUNCTIONAL_IMPL(FloatValue, SimpleType, float_v)
    PSI_TVM_FUNCTIONAL_IMPL(Select, FunctionalValue, select);

    BooleanType::BooleanType(Context& context, const SourceLocation& location)
    : SimpleType(context, hashable_setup<BooleanType>(), location) {
    }
    
    ValuePtr<BooleanType> BooleanType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(BooleanType(context, location));
    }

    BooleanValue::BooleanValue(Context& context, bool value, const SourceLocation& location)
    : SimpleConstructor(Metatype::get(context, location), hashable_setup<BooleanType>(value), location),
    m_value(value) {
    }
    
    ValuePtr<BooleanValue> BooleanValue::get(Context& context, bool value, const SourceLocation& location) {
      return context.get_functional(BooleanValue(context, value, location));
    }
    
    IntegerType::IntegerType(Context& context, Width width, bool is_signed, const SourceLocation& location)
    : SimpleType(context, hashable_setup<IntegerType>(width)(is_signed), location),
    m_width(width),
    m_is_signed(is_signed) {
    }
    
    ValuePtr<IntegerType> IntegerType::get(Context& context, Width width, bool is_signed, const SourceLocation& location) {
      return context.get_functional(IntegerType(context, width, is_signed, location));
    }

    ValuePtr<IntegerType> IntegerType::get_intptr(Context& context, const SourceLocation& location) {
      return get(context, iptr, false, location);
    }
    
    /**
     * Get the number of bits used to represent constants of the given width.
     * This will equal the with of the type except for intptr, where it is
     * machine dependent but treated as 64 bits.
     */
    unsigned IntegerType::value_bits(IntegerType::Width width) {
      switch (width) {
      case IntegerType::i8: return 8;
      case IntegerType::i16: return 16;
      case IntegerType::i32: return 32;
      case IntegerType::i64: return 64;
      case IntegerType::i128: return 128;
      case IntegerType::iptr: return 64;
      default: PSI_FAIL("unexpected integer width");
      }
    }
    
    IntegerValue::IntegerValue(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location)
    : SimpleConstructor(IntegerType::get(context, width, is_signed, location), hashable_setup<IntegerValue>(width)(is_signed)(value), location),
    m_width(width),
    m_is_signed(is_signed),
    m_value(value) {
    }
    
    ValuePtr<IntegerValue> IntegerValue::get(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location) {
      return context.get_functional(IntegerValue(context, width, is_signed, value, location));
    }
    
    ValuePtr<IntegerValue> IntegerValue::get_intptr(Context& context, unsigned n, const SourceLocation& location) {
      return get(context, IntegerType::iptr, false, BigInteger(std::numeric_limits<unsigned>::digits, n), location);
    }

    FloatType::FloatType(Context& context, FloatType::Width width, const SourceLocation& location)
    : SimpleType(context, hashable_setup<FloatType>(width), location),
    m_width(width) {
    }

    ValuePtr<FloatType> FloatType::get(Context& context, Width width, const SourceLocation& location) {
      return context.get_functional(FloatType(context, width, location));
    }
    
    FloatValue::FloatValue(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location)
    : SimpleConstructor(FloatType::get(context, width, location), hashable_setup<FloatValue>(width)(exponent), location),
    m_width(width),
    m_exponent(exponent) {
      PSI_NOT_IMPLEMENTED();
    }
    
    ValuePtr<FloatValue> FloatValue::get(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location) {
      return FloatValue::get(context, width, exponent, mantissa, location);
    }
    
    IntegerUnaryOp::IntegerUnaryOp(const ValuePtr<>& arg, const HashableValueSetup& setup, const SourceLocation& location)
    : UnaryOp(arg->type(), arg, setup, location) {
      if (!isa<IntegerType>(arg->type()))
        throw TvmUserError("Argument to integer unary operation must have integer type");
    }
    
    IntegerBinaryOp::IntegerBinaryOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const HashableValueSetup& setup, const SourceLocation& location)
    : BinaryOp(lhs->type(), lhs, rhs, setup, location) {
      if (!isa<IntegerType>(lhs->type()))
        throw TvmUserError("Argument to integer binary operation must have integer type");
      if (lhs->type() != rhs->type())
        throw TvmUserError("Both parameters to integer binary operation must have the same type");
    }

    IntegerCompareOp::IntegerCompareOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const HashableValueSetup& setup, const SourceLocation& location)
    : BinaryOp(BooleanType::get(lhs->context(), location), lhs, rhs, setup, location) {
      if (!isa<IntegerType>(lhs->type()))
        throw TvmUserError("Argument to integer compare operation must have integer type");
      if (lhs->type() != rhs->type())
        throw TvmUserError("Both parameters to integer compare operation must have the same type");
    }

#define IMPLEMENT_INT_BINARY(name,op_name) PSI_TVM_BINARY_OP_IMPL(name,IntegerBinaryOp,op_name)
#define IMPLEMENT_INT_UNARY(name,op_name) PSI_TVM_UNARY_OP_IMPL(name,IntegerUnaryOp,op_name)
#define IMPLEMENT_INT_COMPARE(name,op_name) PSI_TVM_BINARY_OP_IMPL(name,IntegerCompareOp,op_name)

    IMPLEMENT_INT_BINARY(IntegerAdd, add)
    IMPLEMENT_INT_BINARY(IntegerMultiply, mul)
    IMPLEMENT_INT_BINARY(IntegerDivide, div)
    IMPLEMENT_INT_UNARY(IntegerNegative, neg)
    IMPLEMENT_INT_BINARY(BitAnd, bit_and)
    IMPLEMENT_INT_BINARY(BitOr, bit_or)
    IMPLEMENT_INT_BINARY(BitXor, bit_xor)
    IMPLEMENT_INT_UNARY(BitNot, bit_not)
    IMPLEMENT_INT_COMPARE(IntegerCompareEq, cmp_eq)
    IMPLEMENT_INT_COMPARE(IntegerCompareNe, cmp_ne)
    IMPLEMENT_INT_COMPARE(IntegerCompareGt, cmp_gt)
    IMPLEMENT_INT_COMPARE(IntegerCompareGe, cmp_ge)
    IMPLEMENT_INT_COMPARE(IntegerCompareLt, cmp_lt)
    IMPLEMENT_INT_COMPARE(IntegerCompareLe, cmp_le)

    Select::Select(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location)
    : FunctionalValue(condition->context(), true_value->type(), hashable_setup<Select>(condition)(true_value)(false_value), location),
    m_condition(condition),
    m_true_value(true_value),
    m_false_value(false_value) {
      if (!isa<BooleanType>(m_condition->type()))
        throw TvmUserError("Condition parameter to select must be a boolean");
      if (m_true_value->type() != m_false_value->type())
        throw TvmUserError("Second and third parameters to select must have the same type");
    }

    ValuePtr<Select> Select::get(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location) {
      return condition->context().get_functional(Select(condition, true_value, false_value, location));
    }
  }
}
