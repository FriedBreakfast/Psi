#include "Aggregate.hpp"
#include "FunctionalBuilder.hpp"
#include "Number.hpp"
#include "Function.hpp"
#include "Recursive.hpp"

#include <limits>

namespace Psi {
  namespace Tvm {
    /**
     * \brief Convert a constant size value to an unsigned integer.
     */
    unsigned size_to_unsigned(const ValuePtr<>& value) {
      ValuePtr<IntegerValue> val = dyn_cast<IntegerValue>(value);
      if (!val)
        value->error_context().error_throw(value->location(), "value is not a constant integer");
      
      if (val->width() != IntegerType::iptr)
        value->error_context().error_throw(value->location(), "value is a constant integer but has the wrong width");
      
      return val->value().unsigned_value_checked(value->error_context().bind(value->location()));
    }

    /**
     * \brief Check whether the given matches a constant value.
     * 
     * \throw TvmUserError The type of value was not \c size_type.
     */
    bool size_equals_constant(const ValuePtr<>& value, unsigned c) {
      ValuePtr<IntegerType> ty = dyn_cast<IntegerType>(value->type());
      if (!ty || (ty->width() != IntegerType::iptr))
        value->error_context().error_throw(value->location(), "value is not a size_type integer");
      
      ValuePtr<IntegerValue> val = dyn_cast<IntegerValue>(value);
      if (!val)
        return false;
      
      boost::optional<unsigned> val_int = val->value().unsigned_value();
      if (!val_int)
        return false;
      
      return (*val_int == c);
    }

    BooleanType::BooleanType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    template<typename V>
    void BooleanType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    ValuePtr<> BooleanType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(BooleanType, Type, bool)

    BooleanValue::BooleanValue(Context& context, bool value, const SourceLocation& location)
    : Constructor(context, location),
    m_value(value) {
    }
    
    template<typename V>
    void BooleanValue::visit(V& v) {
      visit_base<Constructor>(v);
    }
    
    ValuePtr<> BooleanValue::check_type() const {
      return FunctionalBuilder::bool_type(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(BooleanValue, Constructor, bool_v)
    
    IntegerType::IntegerType(Context& context, Width width, bool is_signed, const SourceLocation& location)
    : Type(context, location),
    m_width(width),
    m_is_signed(is_signed) {
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

    /**
     * Get the number of 8-bit bytes required to hold the given integer width.
     * 
     * This returns 8 for intptr as the maximum currently expected,
     * but the true value is machine dependent.
     */
    unsigned IntegerType::value_bytes(IntegerType::Width width) {
      switch (width) {
      case IntegerType::i8: return 1;
      case IntegerType::i16: return 2;
      case IntegerType::i32: return 4;
      case IntegerType::i64: return 8;
      case IntegerType::i128: return 16;
      case IntegerType::iptr: return 8;
      default: PSI_FAIL("unexpected integer width");
      }
    }
    
    /**
     * \brief Get the smallest integer type which can hold the specified number of bits
     */
    boost::optional<IntegerType::Width> IntegerType::width_from_bits(unsigned bits) {
      if (bits <= 8)
        return IntegerType::i8;
      else if (bits <= 16)
        return IntegerType::i16;
      else if (bits <= 32)
        return IntegerType::i32;
      else if (bits <= 64)
        return IntegerType::i64;
      else if (bits <= 128)
        return IntegerType::i128;
      else
        return boost::none;
    }

    template<typename V>
    void IntegerType::visit(V& v) {
      visit_base<Type>(v);
      v("width", &IntegerType::m_width)
      ("is_signed", &IntegerType::m_is_signed);
    }
    
    ValuePtr<> IntegerType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(IntegerType, Type, int)
    
    IntegerValue::IntegerValue(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location)
    : Constructor(context, location),
    m_width(width),
    m_is_signed(is_signed),
    m_value(value) {
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
        error_context().error_throw(location(), "Wrong number of bits supplied to integer constant");
      return FunctionalBuilder::int_type(context(), m_width, m_is_signed, location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(IntegerValue, Constructor, int_v)

    FloatType::FloatType(Context& context, FloatType::Width width, const SourceLocation& location)
    : Type(context, location),
    m_width(width) {
    }

    template<typename V>
    void FloatType::visit(V& v) {
      visit_base<Type>(v);
      v("width", &FloatType::m_width);
    }
    
    ValuePtr<> FloatType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(FloatType, Type, float)
    
    FloatValue::FloatValue(Context& context, FloatType::Width width, unsigned exponent, const char *mantissa, const SourceLocation& location)
    : Constructor(context, location),
    m_width(width),
    m_exponent(exponent) {
      PSI_NOT_IMPLEMENTED();
    }
    
    template<typename V>
    void FloatValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("width", &FloatValue::m_width)
      ("true_value", &FloatValue::m_exponent)
      ("mantissa", &FloatValue::m_mantissa);
    }
    
    ValuePtr<> FloatValue::check_type() const {
      return FunctionalBuilder::float_type(context(), m_width, location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(FloatValue, Type, float_v)
    
    IntegerUnaryOp::IntegerUnaryOp(const ValuePtr<>& arg, const SourceLocation& location)
    : UnaryOp(arg, location) {
    }
    
    ValuePtr<> IntegerUnaryOp::check_type() const {
      if (!isa<IntegerType>(parameter()->type()))
        error_context().error_throw(location(), "Argument to integer unary operation must have integer type");
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
        error_context().error_throw(location(), "Argument to integer binary operation must have integer type");
      if (lhs()->type() != rhs()->type())
        error_context().error_throw(location(), "Both parameters to integer binary operation must have the same type");
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
        error_context().error_throw(location(), "Argument to integer compare operation must have integer type");
      if (lhs()->type() != rhs()->type())
        error_context().error_throw(location(), "Both parameters to integer compare operation must have the same type");
      return FunctionalBuilder::bool_type(context(), location());
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
    
    IntegerShiftOp::IntegerShiftOp(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location)
    : BinaryOp(lhs, rhs, location) {
    }
    
    ValuePtr<> IntegerShiftOp::check_type() const {
      if (!isa<IntegerType>(lhs()->type()))
        error_context().error_throw(location(), "bit_shift only works on integer types");
      ValuePtr<IntegerType> shift_type = dyn_cast<IntegerType>(rhs()->type());
      if (!shift_type || shift_type->is_signed() || (shift_type->width() != IntegerType::i32))
        error_context().error_throw(location(), "Bit shift should be an unsigned 32-bit integer");
      return lhs()->type();
    }

    template<typename V>
    void IntegerShiftOp::visit(V& v) {
      visit_base<BinaryOp>(v);
    }

    ValuePtr<> ShiftLeft::check_type() const {return IntegerShiftOp::check_type();}
    PSI_TVM_BINARY_OP_IMPL(ShiftLeft, IntegerShiftOp, shl)
    ValuePtr<> ShiftRight::check_type() const {return IntegerShiftOp::check_type();}
    PSI_TVM_BINARY_OP_IMPL(ShiftRight, IntegerShiftOp, shr)
    
    BitCast::BitCast(const ValuePtr<>& value, const ValuePtr<>& target_type, const SourceLocation& location)
    : FunctionalValue(value->context(), location),
    m_value(value),
    m_target_type(target_type) {
    }
    
    template<typename V>
    void BitCast::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("value", &BitCast::m_value)
      ("target_type", &BitCast::m_target_type);
    }
    
    namespace {
      bool bit_castable_type(const ValuePtr<>& type) {
        return isa<BooleanType>(type)
          || isa<IntegerType>(type)
          || isa<FloatType>(type)
          || isa<PointerType>(type);
      }
    }
    
    ValuePtr<> BitCast::check_type() const {
      if (!bit_castable_type(m_value->type()))
        error_context().error_throw(location(), "bitcast value parameter is not a primitive type");
      if (!bit_castable_type(m_target_type))
        error_context().error_throw(location(), "bitcast type parameter is not primitive");
      return m_target_type;
    }

    PSI_TVM_FUNCTIONAL_IMPL(BitCast, FunctionalValue, bitcast);

    Select::Select(const ValuePtr<>& condition, const ValuePtr<>& true_value, const ValuePtr<>& false_value, const SourceLocation& location)
    : FunctionalValue(condition->context(), location),
    m_condition(condition),
    m_true_value(true_value),
    m_false_value(false_value) {
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
        error_context().error_throw(location(), "Condition parameter to select must be a boolean");
      if (m_true_value->type() != m_false_value->type())
        error_context().error_throw(location(), "Second and third parameters to select must have the same type");
      return m_true_value->type();
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(Select, FunctionalValue, select);
  }
}
