#include "Aggregate.hpp"
#include "FunctionalBuilder.hpp"
#include "Number.hpp"
#include "Function.hpp"

#include <limits>

namespace Psi {
  namespace Tvm {
    BooleanType::BooleanType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    template<typename V>
    void BooleanType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    ValuePtr<> BooleanType::check_type() const {
      return Metatype::get(context(), location());
    }
    
    ValuePtr<BooleanType> BooleanType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(BooleanType(context, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(BooleanType, Type, bool)

    BooleanValue::BooleanValue(Context& context, bool value, const SourceLocation& location)
    : Constructor(context, location),
    m_value(value) {
    }
    
    ValuePtr<BooleanValue> BooleanValue::get(Context& context, bool value, const SourceLocation& location) {
      return context.get_functional(BooleanValue(context, value, location));
    }
    
    template<typename V>
    void BooleanValue::visit(V& v) {
      visit_base<Constructor>(v);
    }
    
    ValuePtr<> BooleanValue::check_type() const {
      return BooleanType::get(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(BooleanValue, Constructor, bool_v)
    
    IntegerType::IntegerType(Context& context, Width width, bool is_signed, const SourceLocation& location)
    : Type(context, location),
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

    template<typename V>
    void IntegerType::visit(V& v) {
      visit_base<Type>(v);
      v("width", &IntegerType::m_width)
      ("is_signed", &IntegerType::m_is_signed);
    }
    
    ValuePtr<> IntegerType::check_type() const {
      return Metatype::get(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(IntegerType, Type, int)
    
    IntegerValue::IntegerValue(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location)
    : Constructor(context, location),
    m_width(width),
    m_is_signed(is_signed),
    m_value(value) {
    }
    
    ValuePtr<IntegerValue> IntegerValue::get(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location) {
      return context.get_functional(IntegerValue(context, width, is_signed, value, location));
    }
    
    ValuePtr<IntegerValue> IntegerValue::get_intptr(Context& context, unsigned n, const SourceLocation& location) {
      return get(context, IntegerType::iptr, false, BigInteger(IntegerType::value_bits(IntegerType::iptr), n), location);
    }

    template<typename V>
    void IntegerValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("width", &IntegerValue::m_width)
      ("is_signed", &IntegerValue::m_is_signed)
      ("value", &IntegerValue::m_value);
    }
    
    ValuePtr<> IntegerValue::check_type() const {
      if (m_value.bits() != IntegerType::value_bits(m_width))
        throw TvmUserError("Wrong number of bits supplied to integer constant");
      return IntegerType::get(context(), m_width, m_is_signed, location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(IntegerValue, Constructor, int_v)

    FloatType::FloatType(Context& context, FloatType::Width width, const SourceLocation& location)
    : Type(context, location),
    m_width(width) {
    }

    ValuePtr<FloatType> FloatType::get(Context& context, Width width, const SourceLocation& location) {
      return context.get_functional(FloatType(context, width, location));
    }
    
    template<typename V>
    void FloatType::visit(V& v) {
      visit_base<Type>(v);
      v("width", &FloatType::m_width);
    }
    
    ValuePtr<> FloatType::check_type() const {
      return Metatype::get(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(FloatType, Type, float)
    
    FloatValue::FloatValue(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location)
    : Constructor(context, location),
    m_width(width),
    m_exponent(exponent) {
      PSI_NOT_IMPLEMENTED();
    }
    
    ValuePtr<FloatValue> FloatValue::get(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location) {
      return FloatValue::get(context, width, exponent, mantissa, location);
    }

    template<typename V>
    void FloatValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("width", &FloatValue::m_width)
      ("true_value", &FloatValue::m_exponent)
      ("mantissa", &FloatValue::m_mantissa);
    }
    
    ValuePtr<> FloatValue::check_type() const {
      return FloatType::get(context(), m_width, location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(FloatValue, Type, float_v)
    
    IntegerUnaryOp::IntegerUnaryOp(const ValuePtr<>& arg, const SourceLocation& location)
    : UnaryOp(arg, location) {
    }
    
    ValuePtr<> IntegerUnaryOp::check_type() const {
      if (!isa<IntegerType>(parameter()->type()))
        throw TvmUserError("Argument to integer unary operation must have integer type");
      return parameter()->type();
    }
    
    template<typename V>
    void IntegerUnaryOp::visit(V& v) {
      visit_base<UnaryOp>(v);
    }
    
    IntegerBinaryOp::IntegerBinaryOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location)
    : BinaryOp(lhs, rhs, location) {
    }
    
    ValuePtr<> IntegerBinaryOp::check_type() const {
      if (!isa<IntegerType>(lhs()->type()))
        throw TvmUserError("Argument to integer binary operation must have integer type");
      if (lhs()->type() != rhs()->type())
        throw TvmUserError("Both parameters to integer binary operation must have the same type");
      return lhs()->type();
    }
    
    template<typename V>
    void IntegerBinaryOp::visit(V& v) {
      visit_base<BinaryOp>(v);
    }

    IntegerCompareOp::IntegerCompareOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location)
    : BinaryOp(lhs, rhs, location) {
    }
    
    ValuePtr<> IntegerCompareOp::check_type() const {
      if (!isa<IntegerType>(lhs()->type()))
        throw TvmUserError("Argument to integer compare operation must have integer type");
      if (lhs()->type() != rhs()->type())
        throw TvmUserError("Both parameters to integer compare operation must have the same type");
      return BooleanType::get(context(), location());
    }
    
    template<typename V>
    void IntegerCompareOp::visit(V& v) {
      visit_base<BinaryOp>(v);
    }

#define IMPLEMENT_INT_BINARY(name,op_name) \
  ValuePtr<> name::check_type() const {return IntegerBinaryOp::check_type();} \
  PSI_TVM_BINARY_OP_IMPL(name,IntegerBinaryOp,op_name)

#define IMPLEMENT_INT_UNARY(name,op_name) \
  ValuePtr<> name::check_type() const {return IntegerUnaryOp::check_type();} \
  PSI_TVM_UNARY_OP_IMPL(name,IntegerUnaryOp,op_name)

#define IMPLEMENT_INT_COMPARE(name,op_name) \
  ValuePtr<> name::check_type() const {return IntegerCompareOp::check_type();} \
  PSI_TVM_BINARY_OP_IMPL(name,IntegerCompareOp,op_name)

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
    : FunctionalValue(condition->context(), location),
    m_condition(condition),
    m_true_value(true_value),
    m_false_value(false_value) {
    }

    ValuePtr<Select> Select::get(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location) {
      return condition->context().get_functional(Select(condition, true_value, false_value, location));
    }
    
    template<typename V>
    void Select::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("condition", &Select::m_condition)
      ("true_value", &Select::m_true_value)
      ("false_value", &Select::m_false_value);
    }
    
    ValuePtr<> Select::check_type() const {
      if (!isa<BooleanType>(m_condition->type()))
        throw TvmUserError("Condition parameter to select must be a boolean");
      if (m_true_value->type() != m_false_value->type())
        throw TvmUserError("Second and third parameters to select must have the same type");
      return m_true_value->type();
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(Select, FunctionalValue, select);
  }
}
