#include "Aggregate.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    const char BooleanType::operation[] = "bool";
    const char BooleanValue::operation[] = "bool_v";
    const char IntegerType::operation[] = "int";
    const char IntegerValue::operation[] = "int_v";
    const char FloatType::operation[] = "float";
    const char FloatValue::operation[] = "float_v";
    const char SelectValue::operation[] = "select";

    FunctionalTypeResult BooleanType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("bool type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get the boolean type
    BooleanType::Ptr BooleanType::get(Context& context) {
      return context.get_functional<BooleanType>(ArrayPtr<Term*>());
    }

    FunctionalTypeResult BooleanValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("bool_v value takes no parameters");
      return FunctionalTypeResult(BooleanType::get(context), false);
    }

    /// \brief Get the boolean type
    BooleanValue::Ptr BooleanValue::get(Context& context, bool value) {
      return context.get_functional<BooleanValue>(ArrayPtr<Term*>(), value);
    }

    FunctionalTypeResult IntegerType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("int type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get an integer type with the specified width and signedness
    IntegerType::Ptr IntegerType::get(Context& context, Width width, bool is_signed) {
      return context.get_functional<IntegerType>(ArrayPtr<Term*>(), Data(width, is_signed));
    }

    /// \brief Get the integer type for intptr.
    IntegerType::Ptr IntegerType::get_size(Context& context) {
      return get(context, iptr, false);
    }

    FunctionalTypeResult IntegerValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("int_v value takes one parameter");
      if (!isa<IntegerType>(parameters[0]))
        throw TvmUserError("int_v parameter is not an integer type");
      return FunctionalTypeResult(parameters[0], false);
    }

    /**
     * Get an integer value from specific data, produced by one of the
     * #parse or #convert functions.
     */
    IntegerValue::Ptr IntegerValue::get(Term* type, const Data& data) {
      return type->context().get_functional<IntegerValue>(StaticArray<Term*,1>(type), data);
    }
    
    /**
     * Get the number of bits used to represent constants of the given width.
     * This will equal the with of the type except for intptr, where it is
     * machine dependent but treated as 64 bits.
     */
    unsigned IntegerValue::value_bits(IntegerType::Width width) {
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

    FunctionalTypeResult FloatType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("float type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get a floating point type of the specified width.
    FloatType::Ptr FloatType::get(Context& context, Width width) {
      return context.get_functional<FloatType>(ArrayPtr<Term*>(), width);
    }

    FunctionalTypeResult FloatValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("float_v value takes one parameter");
      if (!isa<FloatType>(parameters[0]))
        throw TvmUserError("float_v parameter is not an float type");
      return FunctionalTypeResult(parameters[0], false);
    }

    FloatValue::Ptr FloatValue::get(Term* type, const Data& data) {
      return type->context().get_functional<FloatValue>(StaticArray<Term*,1>(type), data);
    }

    namespace {
      FunctionalTypeResult binary_op_type(const char *operation, ArrayPtr<Term*const> parameters) {
        if (parameters.size() != 2)
          throw TvmUserError(std::string(operation) + " expects two operands");

        Term* type = parameters[0]->type();
        if (type != parameters[1]->type())
          throw TvmUserError(std::string(operation) + ": both operands must be of the same type");

        return FunctionalTypeResult(type, parameters[0]->phantom() || parameters[1]->phantom());
      }

      FunctionalTypeResult integer_binary_op_type(const char *operation, ArrayPtr<Term*const> parameters) {
        FunctionalTypeResult result = binary_op_type(operation, parameters);

        if (!isa<IntegerType>(result.type))
          throw TvmUserError(std::string(operation) + ": parameters must be integers");

        return result;
      }

      FunctionalTypeResult unary_op_type(const char *operation, ArrayPtr<Term*const> parameters) {
        if (parameters.size() != 1)
          throw TvmUserError(std::string(operation) + " expects two operands");

        return FunctionalTypeResult(parameters[0]->type(), parameters[0]->phantom());
      }

      FunctionalTypeResult integer_unary_op_type(const char *operation, ArrayPtr<Term*const> parameters) {
        FunctionalTypeResult result = unary_op_type(operation, parameters);

        if (!isa<IntegerType>(result.type))
          throw TvmUserError(std::string(operation) + ": parameter must be an integers");

        return result;
      }

      FunctionalTypeResult integer_cmp_op_type(const char *operation, ArrayPtr<Term*const> parameters) {
        FunctionalTypeResult result = integer_binary_op_type(operation, parameters);
        result.type = BooleanType::get(result.type->context());
        return result;
      }
    }

#define IMPLEMENT_BINARY(name,op_name,type_cb)				\
    const char name::operation[] = op_name;				\
									\
    FunctionalTypeResult name::type(Context&, const Data&, ArrayPtr<Term*const> parameters) { \
      return type_cb(name::operation, parameters);			\
    }                                                                   \
                                                                        \
    name::Ptr name::get(Term *lhs, Term *rhs) {                         \
      Term *parameters[] = {lhs, rhs};					\
      return lhs->context().get_functional<name>(ArrayPtr<Term*const>(parameters, 2)); \
    }

#define IMPLEMENT_UNARY(name,op_name,type_cb)				\
    const char name::operation[] = op_name;				\
									\
    FunctionalTypeResult name::type(Context&, const Data&, ArrayPtr<Term*const> parameters) { \
      return type_cb(name::operation, parameters);			\
    }                                                                   \
                                                                        \
    name::Ptr name::get(Term *parameter) {				\
      Term *parameters[] = {parameter};					\
      return parameter->context().get_functional<name>(ArrayPtr<Term*const>(parameters, 1)); \
    }

#define IMPLEMENT_INT_BINARY(name,op_name) IMPLEMENT_BINARY(name,op_name,integer_binary_op_type)
#define IMPLEMENT_INT_UNARY(name,op_name) IMPLEMENT_UNARY(name,op_name,integer_unary_op_type)
#define IMPLEMENT_INT_COMPARE(name,op_name) IMPLEMENT_BINARY(name,op_name,integer_cmp_op_type)

    IMPLEMENT_INT_BINARY(IntegerAdd, "add")
    IMPLEMENT_INT_BINARY(IntegerMultiply, "mul")
    IMPLEMENT_INT_BINARY(IntegerDivide, "div")
    IMPLEMENT_INT_UNARY(IntegerNegative, "neg")
    IMPLEMENT_INT_BINARY(BitAnd, "bit_and")
    IMPLEMENT_INT_BINARY(BitOr, "bit_or")
    IMPLEMENT_INT_BINARY(BitXor, "bit_xor")
    IMPLEMENT_INT_UNARY(BitNot, "bit_not")
    IMPLEMENT_INT_COMPARE(IntegerCompareEq, "cmp_eq")
    IMPLEMENT_INT_COMPARE(IntegerCompareNe, "cmp_ne")
    IMPLEMENT_INT_COMPARE(IntegerCompareGt, "cmp_gt")
    IMPLEMENT_INT_COMPARE(IntegerCompareGe, "cmp_ge")
    IMPLEMENT_INT_COMPARE(IntegerCompareLt, "cmp_lt")
    IMPLEMENT_INT_COMPARE(IntegerCompareLe, "cmp_le")

    FunctionalTypeResult SelectValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 3)
	throw TvmUserError("select takes three operands");

      if (parameters[0]->type() != BooleanType::get(context))
	throw TvmUserError("select: first operand must be of type bool");

      Term *type = parameters[1]->type();
      if (type != parameters[2]->type())
	throw TvmUserError("select: second and third operands must have the same type");

      return FunctionalTypeResult(type, parameters[0]->phantom() || parameters[1]->phantom() || parameters[2]->phantom());
    }

    SelectValue::Ptr SelectValue::get(Term *condition, Term *true_value, Term *false_value) {
      return condition->context().get_functional<SelectValue>(StaticArray<Term*,3>(condition, true_value, false_value));
    }
  }
}
