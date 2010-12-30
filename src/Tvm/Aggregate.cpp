#include "Aggregate.hpp"
#include "Number.hpp"
#include "Function.hpp"

namespace Psi {
  namespace Tvm {
    namespace {
      FunctionalTypeResult aggregate_type_check(Context& context, ArrayPtr<Term*const> parameters) {
        bool phantom = false;
        for (std::size_t i = 0; i < parameters.size(); ++i) {
          if (!parameters[i]->is_type())
            throw TvmUserError("members of an aggregate type must be types");
          phantom = phantom || parameters[i]->phantom();
        }

        return FunctionalTypeResult(Metatype::get(context), phantom);
      }
    }

    const char EmptyType::operation[] = "empty";
    const char EmptyValue::operation[] = "empty_v";
    const char BlockType::operation[] = "block";
    const char Metatype::operation[] = "type";
    const char MetatypeValue::operation[] = "type_v";
    const char MetatypeSize::operation[] = "sizeof";
    const char MetatypeAlignment::operation[] = "alignof";
    const char PointerType::operation[] = "pointer";
    const char PointerCast::operation[] = "cast";
    const char ArrayType::operation[] = "array";
    const char ArrayValue::operation[] = "array_v";
    const char ArrayElement::operation[] = "array_el";
    const char StructType::operation[] = "struct";
    const char StructValue::operation[] = "struct_v";
    const char StructElement::operation[] = "struct_el";
    const char UnionType::operation[] = "union";
    const char UnionValue::operation[] = "union_v";
    const char UnionElement::operation[] = "union_el";
    const char FunctionSpecialize::operation[] = "specialize";

    FunctionalTypeResult Metatype::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("type type takes no parameters");
      return FunctionalTypeResult(NULL, false);
    }

    Metatype::Ptr Metatype::get(Context& context) {
      return context.get_functional<Metatype>(ArrayPtr<Term*const>());
    }

    FunctionalTypeResult MetatypeValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("type_v takes two parameters");
      Term *intptr_type = IntegerType::get(context, IntegerType::iptr, false);
      if ((parameters[0]->type() != intptr_type) || (parameters[1]->type() != intptr_type))
	throw TvmUserError("both parameters to type_v must be intptr");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    MetatypeValue::Ptr MetatypeValue::get(Term *size, Term *alignment) {
      Term *params[2] = {size, alignment};
      return size->context().get_functional<MetatypeValue>(ArrayPtr<Term*const>(params, 2));
    }

    FunctionalTypeResult MetatypeSize::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("sizeof takes one parameter");
      if (parameters[0]->type() != Metatype::get(context))
	throw TvmUserError("parameter to sizeof must be a type");
      return FunctionalTypeResult(IntegerType::get_size(context), false);
    }

    MetatypeSize::Ptr MetatypeSize::get(Term *target) {
      Term *params[1] = {target};
      return target->context().get_functional<MetatypeSize>(ArrayPtr<Term*const>(params, 1));
    }

    FunctionalTypeResult MetatypeAlignment::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("alignof takes one parameter");
      if (parameters[0]->type() != Metatype::get(context))
	throw TvmUserError("parameter to alignof must be a type");
      return FunctionalTypeResult(IntegerType::get_size(context), false);
    }

    MetatypeAlignment::Ptr MetatypeAlignment::get(Term *target) {
      Term *params[1] = {target};
      return target->context().get_functional<MetatypeAlignment>(ArrayPtr<Term*const>(params, 1));
    }

    FunctionalTypeResult EmptyType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("empty type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    EmptyType::Ptr EmptyType::get(Context& context) {
      return context.get_functional<EmptyType>(ArrayPtr<Term*const>());
    }

    FunctionalTypeResult EmptyValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("empty_v value takes no parameters");
      return FunctionalTypeResult(EmptyType::get(context), false);
    }

    EmptyValue::Ptr EmptyValue::get(Context& context) {
      return context.get_functional<EmptyValue>(ArrayPtr<Term*const>());
    }

    FunctionalTypeResult BlockType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("block type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    BlockType::Ptr BlockType::get(Context& context) {
      return context.get_functional<BlockType>(ArrayPtr<Term*const>());
    }

    FunctionalTypeResult PointerType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("pointer type takes one parameter");
      if (!parameters[0]->is_type())
        throw TvmUserError("pointer argument must be a type");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    PointerType::Ptr PointerType::get(Term *type) {
      Term *params[] = {type};
      return type->context().get_functional<PointerType>(ArrayPtr<Term*const>(params, 1));
    }

    FunctionalTypeResult PointerCast::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
	throw TvmUserError("cast takes two parameters");
      if (isa<PointerType>(parameters[0]->type()))
	throw TvmUserError("first argument to cast is not a pointer");
      if (!parameters[1]->is_type())
	throw TvmUserError("second argument to cast is not a type");
      return FunctionalTypeResult(PointerType::get(parameters[1]), parameters[0]->phantom());
    }

    PointerCast::Ptr PointerCast::get(Term *pointer, Term *target_type) {
      Term *params[] = {pointer, target_type};
      return pointer->context().get_functional<PointerCast>(ArrayPtr<Term*const>(params, 2));
    }

    FunctionalTypeResult ArrayType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("array type term takes two parameters");

      if (!parameters[0]->is_type())
        throw TvmUserError("first argument to array type term is not a type");

      if (parameters[1]->type() != IntegerType::get_size(context))
        throw TvmUserError("second argument to array type term is not an unsigned intptr");

      return FunctionalTypeResult(Metatype::get(context), parameters[0]->phantom() || parameters[1]->phantom());
    }

    ArrayType::Ptr ArrayType::get(Term* element_type, Term* length) {
      Term *params[] = {element_type, length};
      return element_type->context().get_functional<ArrayType>(ArrayPtr<Term*const>(params, 2));
    }

    ArrayType::Ptr ArrayType::get(Term *element_type, unsigned length) {
      IntegerType::Ptr size_type = IntegerType::get_size(element_type->context());
      Term *length_term = IntegerValue::get(size_type, length);
      return get(element_type, length_term);
    }

    FunctionalTypeResult ArrayValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() < 1)
        throw TvmUserError("array values require at least one parameter");

      if (!parameters[0]->is_type())
        throw TvmUserError("first argument to array value is not a type");

      bool phantom = false;
      for (std::size_t i = 1; i < parameters.size(); ++i) {
        if (parameters[i]->type() != parameters[0])
          throw TvmUserError("array value element is of the wrong type");
        phantom = phantom || parameters[i]->phantom();
      }

      PSI_ASSERT(phantom || !parameters[0]->phantom());

      return FunctionalTypeResult(ArrayType::get(parameters[0], parameters.size() - 1), phantom);
    }

    ArrayValue::Ptr ArrayValue::get(Term *element_type, ArrayPtr<Term*const> elements) {
      ScopedTermPtrArray<> parameters(elements.size() + 1);
      parameters[0] = element_type;
      for (std::size_t i = 0, e = elements.size(); i != e; ++i)
        parameters[i+1] = elements[i];
      return element_type->context().get_functional<ArrayValue>(parameters.array());
    }

    FunctionalTypeResult ArrayElement::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
	throw TvmUserError("array_el takes two parameters");

      ArrayType::Ptr array_ty = dyn_cast<ArrayType>(parameters[0]->type());
      if (!array_ty)
	throw TvmUserError("first parameter to array_el does not have array type");

      if (parameters[1]->type() != IntegerType::get_size(context))
	throw TvmUserError("second parameter to array_el is not an intptr");

      return FunctionalTypeResult(array_ty->element_type(), parameters[0]->phantom() || parameters[1]->phantom());
    }

    ArrayElement::Ptr ArrayElement::get(Term *aggregate, Term *index) {
      Term *parameters[] = {aggregate, index};
      return aggregate->context().get_functional<ArrayElement>(ArrayPtr<Term*const>(parameters, 2));
    }

    FunctionalTypeResult StructType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      return aggregate_type_check(context, parameters);
    }

    StructType::Ptr StructType::get(Context& context, ArrayPtr<Term*const> members) {
      return context.get_functional<StructType>(members);
    }

    FunctionalTypeResult StructValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      ScopedTermPtrArray<> member_types(parameters.size());

      bool phantom = false;
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        phantom = phantom || parameters[i]->phantom();
        member_types[i] = parameters[i]->type();
      }

      Term *type = StructType::get(context, member_types.array());
      PSI_ASSERT(phantom == type->phantom());

      return FunctionalTypeResult(type, phantom);
    }

    StructValue::Ptr StructValue::get(Context& context, ArrayPtr<Term*const> elements) {
      return context.get_functional<StructValue>(elements);
    }

    FunctionalTypeResult StructElement::type(Context&, const Data& index, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
	throw TvmUserError("struct_el takes one parameters");

      StructType::Ptr struct_ty = dyn_cast<StructType>(parameters[0]->type());
      if (!struct_ty)
	throw TvmUserError("first argument to struct_el must have struct type");

      if (index >= struct_ty->n_members())
	throw TvmUserError("struct_el member index out of range");

      return FunctionalTypeResult(struct_ty->member_type(index), parameters[0]->phantom());
    }

    StructElement::Ptr StructElement::get(Term *aggregate, unsigned index) {
      Term *parameters[] = {aggregate};
      return aggregate->context().get_functional<StructElement>(ArrayPtr<Term*const>(parameters, 1), index);
    }

    FunctionalTypeResult UnionType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      return aggregate_type_check(context, parameters);
    }

    /// \brief Get the index of the specified type in this union,
    /// or -1 if the type is not present.
    int UnionType::PtrHook::index_of_type(Term *type) const {
      for (std::size_t i = 0; i < n_members(); ++i) {
        if (type == member_type(i))
          return i;
      }
      return -1;
    }

    /// \brief Check whether this union type contains the
    /// specified type.
    bool UnionType::PtrHook::contains_type(Term *type) const {
      return index_of_type(type) >= 0;
    }

    UnionType::Ptr UnionType::get(Context& context, ArrayPtr<Term*const> members) {
      return context.get_functional<UnionType>(members);
    }

    FunctionalTypeResult UnionValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("union_v takes two parameters");

      UnionType::Ptr type = dyn_cast<UnionType>(parameters[0]);
      if (!type)
        throw TvmUserError("first argument to union_v must be a union type");

      if (!type->contains_type(parameters[1]->type()))
        throw TvmUserError("second argument to union_v must correspond to a member of the specified union type");

      return FunctionalTypeResult(type, parameters[0]->phantom() || parameters[1]->phantom());
    }

    UnionValue::Ptr UnionValue::get(UnionType::Ptr type, Term *value) {
      Term *parameters[] = {type, value};
      return type->context().get_functional<UnionValue>(ArrayPtr<Term*const>(parameters, 2));
    }

    FunctionalTypeResult UnionElement::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
	throw TvmUserError("union_el takes two parameters");

      UnionType::Ptr union_ty = dyn_cast<UnionType>(parameters[0]->type());
      if (!union_ty)
	throw TvmUserError("first argument to union_el must have union type");

      if (!union_ty->contains_type(parameters[1]))
	throw TvmUserError("second argument to union_el is not a member of the type of the first");

      return FunctionalTypeResult(parameters[1], parameters[0]->phantom() || parameters[1]->phantom());
    }

    UnionElement::Ptr UnionElement::get(Term *aggregate, Term *member_type) {
      Term *parameters[] = {aggregate, member_type};
      return aggregate->context().get_functional<UnionElement>(ArrayPtr<Term*const>(parameters, 2));
    }

    UnionElement::Ptr UnionElement::get(Term *aggregate, unsigned index) {
      UnionType::Ptr union_ty = dyn_cast<UnionType>(aggregate->type());
      if (!union_ty)
	throw TvmUserError("first argument to union_el must have union type");

      if (index >= union_ty->n_members())
	throw TvmUserError("union_el member index out of range");

      return get(aggregate, union_ty->member_type(index));
    }

    FunctionalTypeResult FunctionSpecialize::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() < 1)
        throw TvmUserError("apply_phantom requires at least one parameter");

      std::size_t n_applied = parameters.size() - 1;

      Term *target = parameters[0];
      PointerType::Ptr target_ptr_type = dyn_cast<PointerType>(target->type());
      if (!target_ptr_type)
	throw TvmUserError("apply_phantom target is not a function pointer");

      FunctionTypeTerm* function_type = dyn_cast<FunctionTypeTerm>(target_ptr_type->target_type());
      if (!function_type)
	throw TvmUserError("apply_phantom target is not a function pointer");

      if (n_applied > function_type->n_phantom_parameters())
        throw TvmUserError("Too many parameters given to apply_phantom");

      ScopedTermPtrArray<> apply_parameters(function_type->n_parameters());
      for (std::size_t i = 0; i < n_applied; ++i)
        apply_parameters[i] = parameters[i+1];

      ScopedTermPtrArray<FunctionTypeParameterTerm> new_parameters(function_type->n_parameters() - n_applied);
      for (std::size_t i = 0; i < new_parameters.size(); ++i) {
        Term* type = function_type->parameter_type_after(apply_parameters.array().slice(0, n_applied + i));
        FunctionTypeParameterTerm* param = context.new_function_type_parameter(type);
        apply_parameters[i + n_applied] = param;
        new_parameters[i] = param;
      }

      Term* result_type = function_type->result_type_after(apply_parameters.array());

      std::size_t result_n_phantom = function_type->n_phantom_parameters() - n_applied;
      std::size_t result_n_normal = function_type->n_parameters() - function_type->n_phantom_parameters();

      FunctionTypeTerm* result_function_type = context.get_function_type
        (function_type->calling_convention(),
         result_type,
         new_parameters.array().slice(0, result_n_phantom),
         new_parameters.array().slice(result_n_phantom, result_n_phantom+result_n_normal));

      return FunctionalTypeResult(PointerType::get(result_function_type), parameters[0]->phantom());
    }
  }
}
