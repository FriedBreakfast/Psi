#include "Aggregate.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    const char BooleanType::operation[] = "bool";
    const char BooleanValue::operation[] = "bool_v";
    const char IntegerType::operation[] = "int";
    const char IntegerValue::operation[] = "int_v";
    const char IntegerAdd::operation[] = "add";
    const char IntegerSubtract::operation[] = "sub";
    const char IntegerMultiply::operation[] = "mul";
    const char IntegerDivide::operation[] = "div";
    const char FloatType::operation[] = "float";
    const char FloatValue::operation[] = "float_v";

    FunctionalTypeResult BooleanType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("bool type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get the boolean type
    BooleanType::Ptr BooleanType::get(Context& context) {
      return context.get_functional<BooleanType>(ArrayPtr<Term*const>());
    }

    FunctionalTypeResult BooleanValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("bool_v value takes no parameters");
      return FunctionalTypeResult(BooleanType::get(context), false);
    }

    /// \brief Get the boolean type
    BooleanValue::Ptr BooleanValue::get(Context& context, bool value) {
      return context.get_functional<BooleanValue>(ArrayPtr<Term*const>(), value);
    }

    FunctionalTypeResult IntegerType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("int type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get an integer type with the specified width and signedness
    IntegerType::Ptr IntegerType::get(Context& context, Width width, bool is_signed) {
      return context.get_functional<IntegerType>(ArrayPtr<Term*const>(), Data(width, is_signed));
    }

    FunctionalTypeResult IntegerValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("int_v value takes one parameter");
      if (!isa<IntegerType>(parameters[0]))
        throw TvmUserError("int_v parameter is not an integer type");
      return FunctionalTypeResult(parameters[0], false);
    }

    /**
     * Get the number of bits required to actually represent the value
     * held by the given data structure.
     *
     * \param is_signed Whether to count the number of bits required
     * to store the value as a signed or unsigned value.
     */
    unsigned IntegerValue::data_bits(const Data& data, bool is_signed) {
      std::size_t i = sizeof(data.bytes) - 1;
      bool negative = is_signed && (data.bytes[i] & 0x80);
      unsigned char trivial_byte = negative ? 0xFF : 0;

      for (; i > 0; --i) {
	if (data.bytes[i] != trivial_byte)
	  break;
      }

      std::size_t bits = i*8;
      unsigned char high_bits = data.bytes[i] ^ trivial_byte;
      for (; high_bits > 0; high_bits >>= 1, ++bits);

      // Signed values require one additional bit to denote the sign
      // (algorithmically this arises because the most significant bit
      // will always be zero after the xor with 0 or 0xFF).
      if (is_signed)
	++bits;

      return bits;
    }      

    /**
     * Parse an integer and convert it to the internal byte array
     * format. Note that this function does not parse minus signs or
     * base-specific prefixes such as '0x' - these should be handled
     * externally and the \c negative and \c base parameters set
     * accordingly.
     *
     * Note that this does not currently detect numerical overflows,
     * i.e. numbers which are too large to represent in the number of
     * bytes a number currently uses.
     *
     * This algorithm is currently very inefficient, with three nested
     * loops.
     */
    IntegerValue::Data IntegerValue::parse(const std::string& value, bool negative, unsigned base) {
      Data result;
      std::fill(result.bytes, result.bytes+sizeof(result.bytes), 0);

      if ((base < 2) || (base > 35))
	throw TvmUserError("Unsupported numerical base, must be between 2 and 35 inclusive");

      for (std::size_t pos = 0; pos < value.size(); ++pos) {
	if (pos > 0) {
	  // multiply current value by the base - base is known to be
	  // a small number so a simple algorithm can be used.
	  unsigned carry = 0;
	  for (std::size_t i = 0; i < sizeof(result.bytes); ++i) {
	    unsigned wide_value = result.bytes[i];
	    wide_value = wide_value*base + carry;
	    carry = wide_value >> 8;
	    result.bytes[i] = wide_value & 0xFF;
	  }
	}

	unsigned char digit = value[pos], digit_value;
	if ((digit >= '0') && (digit <= '9'))
	  digit_value = digit - '0';
	else if ((digit >= 'a') && (digit <= 'z'))
	  digit_value = digit - 'a';
	else if ((digit >= 'A') && (digit <= 'A'))
	  digit_value = digit - 'A';
	else
	  throw TvmUserError("Unrecognised digit in parsing");

	if (digit_value >= base)
	  throw TvmUserError("Digit out of range for base");

	unsigned char carry = digit_value;
	for (std::size_t i = 0; i < sizeof(result.bytes); ++i) {
	  if (result.bytes[i] <= 0xFF - digit_value) {
	    result.bytes[i] += carry;
	    break;
	  } else {
	    result.bytes[i] += carry;
	    carry = 1;
	  }
	}
      }

      if (negative) {
	// two's complement - negate and add one
	for (std::size_t i = 0; i < sizeof(result.bytes); ++i)
	  result.bytes[i] = ~result.bytes[i];

	for (std::size_t i = 0; i < sizeof(result.bytes); ++i) {
	  if (result.bytes[i] != 0xFF) {
	    result.bytes[i] += 1;
	    break;
	  } else {
	    result.bytes[i] = 0;
	  }
	}
      }

      return result;
    }

    /**
     * Convert an integer value to the internal byte array
     * representation. This should only be used for small integer
     * constants which will always fit in the native integer type - do
     * not use this for computed values which could potentially
     * overflow, instead, create constant expressions for those.
     */
    IntegerValue::Data IntegerValue::convert(int value) {
      Data result;
      unsigned u_value = value, i;
      for (i = 0; u_value > 0; ++i, u_value >>= 8)
	result.bytes[i] = static_cast<unsigned char>(u_value);

      unsigned char sign_byte = value >= 0 ? 0 : 0xFF;
      for (; i < sizeof(Data::bytes); ++i)
	result.bytes[i] = sign_byte;

      return result;
    }

    /**
     * Convert an integer value to the internal byte array
     * representation. This should only be used for small integer
     * constants which will always fit in the native integer type - do
     * not use this for computed values which could potentially
     * overflow, instead, create constant expressions for those.
     */
    IntegerValue::Data IntegerValue::convert(unsigned value) {
      Data result;
      unsigned u_value = value, i;
      for (i = 0; u_value > 0; ++i, u_value >>= 8)
	result.bytes[i] = static_cast<unsigned char>(u_value);

      for (; i < sizeof(Data::bytes); ++i)
	result.bytes[i] = 0;

      return result;
    }

    /**
     * Get an integer value from specific data, produced by one of the
     * #parse or #convert functions.
     */
    IntegerValue::Ptr IntegerValue::get(IntegerType::Ptr type, const Data& data) {
      Term *parameters[] = {type};
      return type->context().get_functional<IntegerValue>(ArrayPtr<Term*const>(parameters, 1), data);
    }

    /**
     * Get an integer value by parsing a string containing the
     * value. \see #parse for a description of the options.
     */
    IntegerValue::Ptr IntegerValue::get(IntegerType::Ptr type, const std::string& value, bool negative, unsigned base) {
      return get(type, parse(value, negative, base));
    }

    /**
     * Get an integer value from a simple int. \see #convert(int) for
     * details.
     */
    IntegerValue::Ptr IntegerValue::get(IntegerType::Ptr type, int value) {
      return get(type, convert(value));
    }

    /**
     * Get an integer value from a simple int. \see #convert(int) for
     * details.
     */
    IntegerValue::Ptr IntegerValue::get(IntegerType::Ptr type, unsigned value) {
      return get(type, convert(value));
    }

    FunctionalTypeResult FloatType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("float type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get a floating point type of the specified width.
    FloatType::Ptr FloatType::get(Context& context, Width width) {
      return context.get_functional<FloatType>(ArrayPtr<Term*const>(), width);
    }

    FunctionalTypeResult FloatValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("float_v value takes one parameter");
      if (!isa<FloatType>(parameters[0]))
        throw TvmUserError("float_v parameter is not an float type");
      return FunctionalTypeResult(parameters[0], false);
    }

    FloatValue::Ptr FloatValue::get(FloatType::Ptr type, const Data& data) {
      Term *parameters[] = {type};
      return type->context().get_functional<FloatValue>(ArrayPtr<Term*const>(parameters, 1), data);
    }

    namespace {
      FunctionalTypeResult binary_op_type(ArrayPtr<Term*const> parameters) {
        if (parameters.size() != 2)
          throw TvmUserError("binary arithmetic operation expects two operands");

        Term* type = parameters[0]->type();
        if (type != parameters[1]->type())
          throw TvmUserError("type mismatch between operands to binary arithmetic operation");

        return FunctionalTypeResult(type, parameters[0]->phantom() || parameters[1]->phantom());
      }

      FunctionalTypeResult integer_binary_op_type(ArrayPtr<Term*const> parameters) {
        FunctionalTypeResult result = binary_op_type(parameters);

        if (!isa<IntegerType>(result.type))
          throw TvmUserError("parameters to integer binary arithmetic operation were not integers");

        return result;
      }
    }

#define IMPLEMENT_BINARY(name,type_cb)                                  \
    FunctionalTypeResult name::type(Context&, const Data&, ArrayPtr<Term*const> parameters) { \
      return type_cb(parameters);                                       \
    }                                                                   \
                                                                        \
    name::Ptr name::get(Term *lhs, Term *rhs) {                         \
    Term *parameters[] = {lhs, rhs};                                    \
    return lhs->context().get_functional<name>(ArrayPtr<Term*const>(parameters, 2)); \
    }

#define IMPLEMENT_INT_BINARY(name) IMPLEMENT_BINARY(name,integer_binary_op_type)

    IMPLEMENT_INT_BINARY(IntegerAdd)
    IMPLEMENT_INT_BINARY(IntegerSubtract)
    IMPLEMENT_INT_BINARY(IntegerMultiply)
    IMPLEMENT_INT_BINARY(IntegerDivide)
  }
}
