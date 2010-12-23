#include "Aggregate.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    const char BooleanType::operation[] = "bool";
    const char BooleanValue::operation[] = "bool_v";
    const char IntegerType::operation[] = "int";
    const char IntegerValue::operation[] = "int_v";
    const char IntegerAdd::operation[] = "add";
    const char IntegerSubtract::operation[] = "subtract";
    const char IntegerMultiply::operation[] = "multiply";
    const char IntegerDivide::operation[] = "divide";

    FunctionalTypeResult BooleanType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("bool type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get the boolean type
    BooleanType::Ptr BooleanType::get(Context& context) {
      return context.get_functional<BooleanType>(ArrayPtr<Term*const>(0, 0));
    }

    FunctionalTypeResult BooleanValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("bool_v value takes no parameters");
      return FunctionalTypeResult(BooleanType::get(context), false);
    }

    /// \brief Get the boolean type
    BooleanValue::Ptr BooleanValue::get(Context& context, bool value) {
      return context.get_functional<BooleanValue>(ArrayPtr<Term*const>(0, 0), value);
    }

    FunctionalTypeResult IntegerType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("int type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    /// \brief Get an integer type with the specified width and signedness
    IntegerType::Ptr IntegerType::get(Context& context, Width width, bool is_signed) {
      return context.get_functional<IntegerType>(ArrayPtr<Term*const>(0, 0), Data(width, is_signed));
    }

    FunctionalTypeResult IntegerValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("int_v value takes one parameter");
      if (!isa<IntegerType>(parameters[0]))
        throw TvmUserError("int_v parameter is not an integer type");
      return FunctionalTypeResult(parameters[0], false);
    }

    IntegerValue::Ptr IntegerValue::get(IntegerType::Ptr type, uint64_t value) {
      Term *parameters[] = {type};
      return type->context().get_functional<IntegerValue>(ArrayPtr<Term*const>(parameters, 1), value);
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
