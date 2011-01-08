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
    const char ByteType::operation[] = "byte";
    const char Metatype::operation[] = "type";
    const char MetatypeValue::operation[] = "type_v";
    const char MetatypeSize::operation[] = "sizeof";
    const char MetatypeAlignment::operation[] = "alignof";
    const char UndefinedValue::operation[] = "undef";
    const char PointerType::operation[] = "pointer";
    const char PointerCast::operation[] = "cast";
    const char PointerOffset::operation[] = "offset";
    const char ArrayType::operation[] = "array";
    const char ArrayValue::operation[] = "array_v";
    const char ArrayElement::operation[] = "array_el";
    const char ArrayElementPtr::operation[] = "array_ep";
    const char StructType::operation[] = "struct";
    const char StructValue::operation[] = "struct_v";
    const char StructElement::operation[] = "struct_el";
    const char StructElementPtr::operation[] = "struct_ep";
    const char StructElementOffset::operation[] = "struct_eo";
    const char UnionType::operation[] = "union";
    const char UnionValue::operation[] = "union_v";
    const char UnionElement::operation[] = "union_el";
    const char UnionElementPtr::operation[] = "union_ep";
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
      return size->context().get_functional<MetatypeValue>(StaticArray<Term*,2>(size, alignment));
    }

    FunctionalTypeResult MetatypeSize::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("sizeof takes one parameter");
      if (parameters[0]->type() != Metatype::get(context))
	throw TvmUserError("parameter to sizeof must be a type");
      return FunctionalTypeResult(IntegerType::get_size(context), false);
    }

    MetatypeSize::Ptr MetatypeSize::get(Term *target) {
      return target->context().get_functional<MetatypeSize>(StaticArray<Term*,1>(target));
    }

    FunctionalTypeResult MetatypeAlignment::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("alignof takes one parameter");
      if (parameters[0]->type() != Metatype::get(context))
	throw TvmUserError("parameter to alignof must be a type");
      return FunctionalTypeResult(IntegerType::get_size(context), false);
    }

    MetatypeAlignment::Ptr MetatypeAlignment::get(Term *target) {
      return target->context().get_functional<MetatypeAlignment>(StaticArray<Term*,1>(target));
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

    FunctionalTypeResult ByteType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 0)
        throw TvmUserError("byte type takes no parameters");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    ByteType::Ptr ByteType::get(Context& context) {
      return context.get_functional<ByteType>(ArrayPtr<Term*const>());
    }
    
    FunctionalTypeResult UndefinedValue::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("undef takes one parameter");
      if (!parameters[0]->is_type())
        throw TvmUserError("parameter to undef must be a type");
      return FunctionalTypeResult(parameters[0], parameters[0]->phantom());
    }
    
    UndefinedValue::Ptr UndefinedValue::get(Term* type) {
      return type->context().get_functional<UndefinedValue>(StaticArray<Term*,1>(type));
    }

    FunctionalTypeResult PointerType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("pointer type takes one parameter");
      if (!parameters[0]->is_type())
        throw TvmUserError("pointer argument must be a type");
      return FunctionalTypeResult(Metatype::get(context), false);
    }

    PointerType::Ptr PointerType::get(Term *type) {
      return type->context().get_functional<PointerType>(StaticArray<Term*,1>(type));
    }

    FunctionalTypeResult PointerCast::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
	throw TvmUserError("cast takes two parameters");
      if (!isa<PointerType>(parameters[0]->type()))
	throw TvmUserError("first argument to cast is not a pointer");
      if (!parameters[1]->is_type())
	throw TvmUserError("second argument to cast is not a type");
      return FunctionalTypeResult(PointerType::get(parameters[1]), parameters[0]->phantom());
    }

    PointerCast::Ptr PointerCast::get(Term *pointer, Term *target_type) {
      return pointer->context().get_functional<PointerCast>(StaticArray<Term*,2>(pointer, target_type));
    }
    
    FunctionalTypeResult PointerOffset::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("offset takes two parameters");
      if (!isa<PointerType>(parameters[0]->type()))
        throw TvmUserError("first argument to offset is not a pointer");
      IntegerType::Ptr int_ty = dyn_cast<IntegerType>(parameters[1]->type());
      if (!int_ty || (int_ty->width() != IntegerType::iptr))
        throw TvmUserError("second argument to offset is not an intptr or uintptr");
      return FunctionalTypeResult(parameters[0]->type(), parameters[0]->phantom() || parameters[1]->phantom());
    }
    
    /**
     * Get an offset term.
     * 
     * \param ptr Pointer to offset from.
     * 
     * \param offset Offset in units of the pointed-to type.
     */
    PointerOffset::Ptr PointerOffset::get(Term *ptr, Term *offset) {
      return ptr->context().get_functional<PointerOffset>(StaticArray<Term*,2>(ptr, offset));
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
      return element_type->context().get_functional<ArrayType>(StaticArray<Term*,2>(element_type, length));
    }

    ArrayType::Ptr ArrayType::get(Term *element_type, unsigned length) {
      IntegerType::Ptr size_type = IntegerType::get_size(element_type->context());
      Term *length_term = IntegerValue::get(size_type, IntegerValue::convert(length));
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
      ScopedArray<Term*> parameters(elements.size() + 1);
      parameters[0] = element_type;
      for (std::size_t i = 0, e = elements.size(); i != e; ++i)
        parameters[i+1] = elements[i];
      return element_type->context().get_functional<ArrayValue>(parameters);
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
      return aggregate->context().get_functional<ArrayElement>(StaticArray<Term*,2>(aggregate, index));
    }
    
    FunctionalTypeResult ArrayElementPtr::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("array_ep takes two parameters");
      
      PointerType::Ptr array_ptr_ty = dyn_cast<PointerType>(parameters[0]->type());
      if (!array_ptr_ty)
        throw TvmUserError("first parameter to array_ep is not a pointer");

      ArrayType::Ptr array_ty = dyn_cast<ArrayType>(array_ptr_ty->target_type());
      if (!array_ty)
        throw TvmUserError("first parameter to array_ep is not a pointer to an array");

      if (parameters[1]->type() != IntegerType::get_size(context))
        throw TvmUserError("second parameter to array_ep is not an intptr");

      return FunctionalTypeResult(PointerType::get(array_ty->element_type()), parameters[0]->phantom() || parameters[1]->phantom());
    }

    ArrayElementPtr::Ptr ArrayElementPtr::get(Term *aggregate_ptr, Term *index) {
      return aggregate_ptr->context().get_functional<ArrayElementPtr>(StaticArray<Term*,2>(aggregate_ptr, index));
    }

    FunctionalTypeResult StructType::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      return aggregate_type_check(context, parameters);
    }

    StructType::Ptr StructType::get(Context& context, ArrayPtr<Term*const> members) {
      return context.get_functional<StructType>(members);
    }

    FunctionalTypeResult StructValue::type(Context& context, const Data&, ArrayPtr<Term*const> parameters) {
      ScopedArray<Term*> member_types(parameters.size());

      bool phantom = false;
      for (std::size_t i = 0; i < parameters.size(); ++i) {
        phantom = phantom || parameters[i]->phantom();
        member_types[i] = parameters[i]->type();
      }

      Term *type = StructType::get(context, member_types);
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

      if (index.value() >= struct_ty->n_members())
	throw TvmUserError("struct_el member index out of range");

      return FunctionalTypeResult(struct_ty->member_type(index.value()), parameters[0]->phantom());
    }

    StructElement::Ptr StructElement::get(Term *aggregate, unsigned index) {
      return aggregate->context().get_functional<StructElement>(StaticArray<Term*,1>(aggregate), index);
    }

    FunctionalTypeResult StructElementPtr::type(Context&, const Data& index, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("struct_ep takes one parameter");
      
      PointerType::Ptr struct_ptr_ty = dyn_cast<PointerType>(parameters[0]->type());
      if (!struct_ptr_ty)
        throw TvmUserError("first argument to struct_ep is not a pointer");

      StructType::Ptr struct_ty = dyn_cast<StructType>(struct_ptr_ty->target_type());
      if (!struct_ty)
        throw TvmUserError("first argument to struct_ep is not a pointer to a struct");

      if (index.value() >= struct_ty->n_members())
        throw TvmUserError("struct_ep member index out of range");

      return FunctionalTypeResult(PointerType::get(struct_ty->member_type(index.value())), parameters[0]->phantom());
    }

    StructElementPtr::Ptr StructElementPtr::get(Term *aggregate_ptr, unsigned index) {
      return aggregate_ptr->context().get_functional<StructElementPtr>(StaticArray<Term*,1>(aggregate_ptr), index);
    }

    FunctionalTypeResult StructElementOffset::type(Context& context, const Data& index, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 1)
        throw TvmUserError("struct_eo takes one parameter");
      
      StructType::Ptr struct_ty = dyn_cast<StructType>(parameters[0]);
      if (!struct_ty)
        throw TvmUserError("first argument to struct_eo is not a struct type");
      
      if (index.value() >= struct_ty->n_members())
        throw TvmUserError("struct_eo member index out of range");

      return FunctionalTypeResult(IntegerType::get_size(context), parameters[0]->phantom());
    }

    StructElementOffset::Ptr StructElementOffset::get(Term *type, unsigned index) {
      return type->context().get_functional<StructElementOffset>(StaticArray<Term*,1>(type), index);
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

    UnionValue::Ptr UnionValue::get(Term* type, Term* value) {
      return type->context().get_functional<UnionValue>(StaticArray<Term*,2>(type, value));
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
      return aggregate->context().get_functional<UnionElement>(StaticArray<Term*,2>(aggregate, member_type));
    }

    FunctionalTypeResult UnionElementPtr::type(Context&, const Data&, ArrayPtr<Term*const> parameters) {
      if (parameters.size() != 2)
        throw TvmUserError("union_ep takes two parameters");
      
      PointerType::Ptr union_ptr_ty = dyn_cast<PointerType>(parameters[0]->type());
      if (!union_ptr_ty)
        throw TvmUserError("first argument to union_ep is not a pointer");

      UnionType::Ptr union_ty = dyn_cast<UnionType>(union_ptr_ty->target_type());
      if (!union_ty)
        throw TvmUserError("first argument to union_ep is not a pointer to a union");

      if (!union_ty->contains_type(parameters[1]))
        throw TvmUserError("second argument to union_ep is not a member of the type of the first");

      return FunctionalTypeResult(PointerType::get(parameters[1]), parameters[0]->phantom() || parameters[1]->phantom());
    }

    UnionElementPtr::Ptr UnionElementPtr::get(Term *aggregate_ptr, Term *member_type) {
      return aggregate_ptr->context().get_functional<UnionElementPtr>(StaticArray<Term*,2>(aggregate_ptr, member_type));
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

      ScopedArray<Term*> apply_parameters(function_type->n_parameters());
      for (std::size_t i = 0; i < n_applied; ++i)
        apply_parameters[i] = parameters[i+1];

      ScopedArray<FunctionTypeParameterTerm*> new_parameters(function_type->n_parameters() - n_applied);
      for (std::size_t i = 0; i < new_parameters.size(); ++i) {
        Term* type = function_type->parameter_type_after(apply_parameters.slice(0, n_applied + i));
        FunctionTypeParameterTerm* param = context.new_function_type_parameter(type);
        apply_parameters[i + n_applied] = param;
        new_parameters[i] = param;
      }

      Term* result_type = function_type->result_type_after(apply_parameters);

      std::size_t result_n_phantom = function_type->n_phantom_parameters() - n_applied;
      std::size_t result_n_normal = function_type->n_parameters() - function_type->n_phantom_parameters();

      FunctionTypeTerm* result_function_type = context.get_function_type
        (function_type->calling_convention(),
         result_type,
         new_parameters.slice(0, result_n_phantom),
         new_parameters.slice(result_n_phantom, result_n_phantom+result_n_normal));

      return FunctionalTypeResult(PointerType::get(result_function_type), parameters[0]->phantom());
    }
  }
}
