#include "Aggregate.hpp"
#include "Number.hpp"
#include "Function.hpp"
#include "FunctionalBuilder.hpp"
#include "Utility.hpp"
#include <boost/concept_check.hpp>
#include <boost/concept_check.hpp>

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_IMPL(Metatype, FunctionalValue, type)

    Metatype::Metatype(Context& context, const SourceLocation& location)
    : FunctionalValue(context, ValuePtr<>(), hashable_setup<Metatype>(), location) {
    }
    
    ValuePtr<> Metatype::get(Context& context, const SourceLocation& location) {
      return context.get_functional(Metatype(context, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(MetatypeValue, Constructor, type_v)

    MetatypeValue::MetatypeValue(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location)
    : Constructor(Metatype::get(size->context(), location),
                  hashable_setup<MetatypeValue>(size)(alignment), location),
    m_size(size),
    m_alignment(alignment) {
      ValuePtr<> intptr_type = IntegerType::get_intptr(context(), location);
      if (m_size->type() != intptr_type)
        throw TvmUserError("first parameter to type_v must be intptr");
      if (m_alignment->type() != intptr_type)
        throw TvmUserError("second parameter to type_v must be intptr");
    }

    PSI_TVM_FUNCTIONAL_IMPL(MetatypeSize, UnaryOp, sizeof)
    
    MetatypeSize::MetatypeSize(const ValuePtr<>& target, const SourceLocation& location)
    : UnaryOp(IntegerType::get_intptr(target->context(), location), target, hashable_setup<MetatypeSize>(), location) {
      if (target->type() != Metatype::get(context(), location))
        throw TvmUserError("Parameter to sizeof must be a type");
    }

    ValuePtr<> MetatypeSize::get(const ValuePtr<>& target, const SourceLocation& location) {
      return target->context().get_functional(MetatypeSize(target, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(MetatypeAlignment, UnaryOp, alignof)

    MetatypeAlignment::MetatypeAlignment(const ValuePtr<>& target, const SourceLocation& location)
    : UnaryOp(IntegerType::get_intptr(target->context(), location), target, hashable_setup<MetatypeAlignment>(), location) {
      if (target->type() != Metatype::get(context(), location))
        throw TvmUserError("Parameter to alignof must be a type");
    }

    ValuePtr<> MetatypeAlignment::get(const ValuePtr<>& target, const SourceLocation& location) {
      return target->context().get_functional(MetatypeAlignment(target, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(EmptyType, Type, empty)
    
    EmptyType::EmptyType(Context& context, const SourceLocation& location)
    : Type(context, hashable_setup<EmptyType>(), location) {
    }
    
    ValuePtr<EmptyType> EmptyType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(EmptyType(context, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(EmptyValue, Constructor, empty_v)

    EmptyValue::EmptyValue(Context& context, const SourceLocation& location)
    : Constructor(EmptyType::get(context, location), hashable_setup<EmptyValue>(), location) {
    }
    
    ValuePtr<EmptyValue> EmptyValue::get(Context& context, const SourceLocation& location) {
      return context.get_functional(EmptyValue(context, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(BlockType, Type, block)
    
    BlockType::BlockType(Context& context, const SourceLocation& location)
    : Type(context, hashable_setup<BlockType>(), location) {
    }
    
    ValuePtr<BlockType> BlockType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(BlockType(context, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(ByteType, Type, byte)
    
    ByteType::ByteType(Context& context, const SourceLocation& location)
    : Type(context, hashable_setup<ByteType>(), location) {
    }
    
    ValuePtr<ByteType> ByteType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(ByteType(context, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UndefinedValue, SimpleOp, undef);

    UndefinedValue::UndefinedValue(const ValuePtr<>& type, const SourceLocation& location)
    : SimpleOp(type, hashable_setup<UndefinedValue>(type), location) {
    }
    
    ValuePtr<> UndefinedValue::get(const ValuePtr<>& type, const SourceLocation& location) {
      return type->context().get_functional(UndefinedValue(type, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(PointerType, Type, pointer)
    
    PointerType::PointerType(const ValuePtr<>& target_type, const SourceLocation& location)
    : Type(target_type->context(), hashable_setup<PointerType>(target_type), location),
    m_target_type(target_type) {
      if (!target_type->is_type())
        throw TvmUserError("pointer argument must be a type");
    }
    
    ValuePtr<> PointerType::get(const ValuePtr<>& target_type, const SourceLocation& location) {
      return target_type->context().get_functional(PointerType(target_type, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(PointerCast, AggregateOp, cast)
    
    PointerCast::PointerCast(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const SourceLocation& location)
    : AggregateOp(PointerType::get(pointer, location), hashable_setup<PointerCast>(pointer)(target_type), location),
    m_pointer(pointer),
    m_target_type(target_type) {
      if (!isa<PointerType>(pointer->type()))
        throw TvmUserError("first argument to cast is not a pointer");
      if (!target_type->is_type())
        throw TvmUserError("second argument to cast is not a type");
    }
    
    ValuePtr<> PointerCast::get(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const SourceLocation& location) {
      return pointer->context().get_functional(PointerCast(pointer, target_type, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(PointerOffset, AggregateOp, offset)
    
    PointerOffset::PointerOffset(const ValuePtr<>& pointer, const ValuePtr<>& offset, const SourceLocation& location)
    : AggregateOp(pointer->type(), hashable_setup<PointerOffset>(pointer)(offset), location),
    m_pointer(pointer),
    m_offset(offset) {
      if (!isa<PointerType>(pointer))
        throw TvmUserError("first argument to offset is not a pointer");
      ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(offset->type());
      if (!int_ty || (int_ty->width() != IntegerType::iptr))
        throw TvmUserError("second argument to offset is not an intptr or uintptr");
    }
    
    /**
     * Get an offset term.
     * 
     * \param ptr Pointer to offset from.
     * 
     * \param offset Offset in units of the pointed-to type.
     */
    ValuePtr<> PointerOffset::get(const ValuePtr<>& pointer, const ValuePtr<>& offset, const SourceLocation& location) {
      return pointer->context().get_functional(PointerOffset(pointer, offset, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(ArrayType, Type, array);
    
    ArrayType::ArrayType(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location)
    : Type(element_type->context(), hashable_setup<ArrayType>(element_type)(length), location),
    m_element_type(element_type),
    m_length(length) {
    }
    
    ValuePtr<> ArrayType::get(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location) {
      return element_type->context().get_functional(ArrayType(element_type, length, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayValue, Constructor, array_v)
    
    namespace {
      HashableValueSetup array_hashable_setup(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements) {
        HashableValueSetup vh = hashable_setup<ArrayType>(element_type);
        for (std::vector<ValuePtr<> >::const_iterator ii = elements.begin(), ie = elements.end(); ii != ie; ++ii)
          vh.combine(*ii);
        return vh;
      }
    }
    
    ArrayValue::ArrayValue(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location)
    : Constructor(ArrayType::get(element_type, IntegerValue::get_intptr(element_type->context(), elements.size()), location), array_hashable_setup(element_type, elements), location),
    m_element_type(element_type),
    m_elements(elements) {
      if (!element_type->is_type())
        throw TvmUserError("first argument to array value is not a type");

      for (std::vector<ValuePtr<> >::const_iterator ii = elements.begin(), ie = elements.end(); ii != ie; ++ii) {
        if ((*ii)->type() != element_type)
          throw TvmUserError("array value element is of the wrong type");
      }
    }

    ValuePtr<> ArrayValue::get(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location) {
      return element_type->context().get_functional(ArrayValue(element_type, elements, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayElement, AggregateOp, array_el)
    
    namespace {
      ValuePtr<> array_element_type(const ValuePtr<>& aggregate) {
        ValuePtr<ArrayType> ty = dyn_cast<ArrayType>(aggregate->type());
        if (!ty)
          throw TvmUserError("first parameter to array_el does not have array type");
        return ty->element_type();
      }
    }
    
    ArrayElement::ArrayElement(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location)
    : AggregateOp(array_element_type(aggregate), hashable_setup<ArrayElement>(aggregate)(index), location),
    m_aggregate(aggregate),
    m_index(index) {
      if (index->type() != IntegerType::get_intptr(index->context(), location))
        throw TvmUserError("second parameter to array_el is not an intptr");
    }

    ValuePtr<> ArrayElement::get(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location) {
      return aggregate->context().get_functional(ArrayElement(aggregate, index, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayElementPtr, AggregateOp, array_ep)
    
    namespace {
      ValuePtr<> array_element_ptr_type(const ValuePtr<>& aggregate_ptr, const SourceLocation& location) {
        if (ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(aggregate_ptr->type()))
          if (ValuePtr<ArrayType> ty = dyn_cast<ArrayType>(ptr_ty->target_type()))
            return PointerType::get(ty->element_type(), location);
        throw TvmUserError("first parameter to array_ep does not have pointer to array type");
      }
    }
    
    ArrayElementPtr::ArrayElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location)
    : AggregateOp(array_element_ptr_type(aggregate_ptr, location), hashable_setup<ArrayElementPtr>(aggregate_ptr)(index), location),
    m_aggregate_ptr(aggregate_ptr),
    m_index(index) {
      if (index->type() != IntegerType::get_intptr(index->context(), location))
        throw TvmUserError("second parameter to array_ep is not an intptr");
    }

    ValuePtr<> ArrayElementPtr::get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location) {
      return aggregate_ptr->context().get_functional(ArrayElementPtr(aggregate_ptr, index, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructType, Type, struct)
    
    namespace {
      HashableValueSetup struct_type_hashable_setup(const std::vector<ValuePtr<> >& members) {
        HashableValueSetup vh = hashable_setup<StructType>();
        for (std::vector<ValuePtr<> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii) {
          if (!(*ii)->is_type())
            throw TvmUserError("struct argument is not a type");
          vh.combine(*ii);
        }
        return vh;
      }
    }
    
    StructType::StructType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Type(context, struct_type_hashable_setup(members), location),
    m_members(members) {
    }

    ValuePtr<StructType> StructType::get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location) {
      return context.get_functional(StructType(context, members, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructValue, Constructor, struct_v)
    
    namespace {
      ValuePtr<> struct_value_type(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location) {
        std::vector<ValuePtr<> > member_types;
        member_types.reserve(members.size());
        for (std::vector<ValuePtr<> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii)
          member_types.push_back((*ii)->type());
        return StructType::get(context, member_types, location);
      }
      
      HashableValueSetup struct_value_hashable_setup(const std::vector<ValuePtr<> >& members) {
        HashableValueSetup vh = hashable_setup<StructValue>();
        for (std::vector<ValuePtr<> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii)
          vh.combine(*ii);
        return vh;
      }
    }
    
    StructValue::StructValue(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Constructor(struct_value_type(context, members, location), struct_value_hashable_setup(members), location),
    m_members(members) {
    }

    ValuePtr<> StructValue::get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location) {
      return context.get_functional(StructValue(context, members, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElement, AggregateOp, struct_el)
    
    namespace {
      ValuePtr<> struct_element_type(const ValuePtr<>& aggregate, unsigned index) {
        ValuePtr<StructType> ty = dyn_cast<StructType>(aggregate->type());
        if (!ty)
          throw TvmUserError("parameter to struct_el does not have struct type");
        if (index >= ty->n_members())
          throw TvmUserError("struct_el member index out of range");
        return ty->member_type(index);
      }
    }
    
    StructElement::StructElement(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location)
    : AggregateOp(struct_element_type(aggregate, index), hashable_setup<StructElement>(aggregate)(index), location),
    m_aggregate(aggregate),
    m_index(index) {
    }

    ValuePtr<> StructElement::get(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location) {
      return aggregate->context().get_functional(StructElement(aggregate, index, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElementPtr, AggregateOp, struct_ep)
    
    namespace {
      ValuePtr<> struct_element_ptr_type(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location) {
        ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(aggregate_ptr->type());
        if (!ptr_ty)
          throw TvmUserError("first argument to struct_ep is not a pointer");
        
        ValuePtr<StructType> ty = dyn_cast<StructType>(ptr_ty->target_type());
        if (!ty)
          throw TvmUserError("first argument to struct_ep is not a pointer to a struct");
        
        if (index >= ty->n_members())
          throw TvmUserError("struct_el member index out of range");
        return PointerType::get(ty->member_type(index), location);
      }
    }
    
    StructElementPtr::StructElementPtr(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location)
    : AggregateOp(struct_element_ptr_type(aggregate_ptr, index, location), hashable_setup<StructElementPtr>(aggregate_ptr)(index), location),
    m_aggregate_ptr(aggregate_ptr),
    m_index(index) {
    }

    ValuePtr<> StructElementPtr::get(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location) {
      return aggregate_ptr->context().get_functional(StructElementPtr(aggregate_ptr, index, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElementOffset, AggregateOp, struct_eo)
    
    StructElementOffset::StructElementOffset(const ValuePtr<>& aggregate_type, unsigned index, const SourceLocation& location)
    : AggregateOp(IntegerType::get_intptr(aggregate_type->context(), location), hashable_setup<StructElementOffset>(aggregate_type)(index), location),
    m_aggregate_type(dyn_cast<StructType>(aggregate_type)),
    m_index(index) {
      if (!m_aggregate_type)
        throw TvmUserError("first argument to struct_eo is not a struct type");
      if (index >= m_aggregate_type->n_members())
        throw TvmUserError("struct_eo member index out of range");
    }
    
    ValuePtr<> StructElementOffset::get(const ValuePtr<>& aggregate_type, unsigned index, const SourceLocation& location) {
      return aggregate_type->context().get_functional(StructElementOffset(aggregate_type, index, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UnionType, Type, union)

    namespace {
      HashableValueSetup union_type_hashable_setup(const std::vector<ValuePtr<> >& members) {
        HashableValueSetup vh = hashable_setup<UnionType>();
        for (std::vector<ValuePtr<> >::const_iterator ii = members.begin(), ie = members.end(); ii != ie; ++ii) {
          if (!(*ii)->is_type())
            throw TvmUserError("union argument is not a type");
          vh.combine(*ii);
        }
        return vh;
      }
    }

    UnionType::UnionType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Type(context, union_type_hashable_setup(members), location),
    m_members(members) {
    }
    
    ValuePtr<> UnionType::get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location) {
      return context.get_functional(UnionType(context, members, location));
    }

    /// \brief Get the index of the specified type in this union,
    /// or -1 if the type is not present.
    int UnionType::index_of_type(const ValuePtr<>& type) const {
      for (std::size_t i = 0; i < n_members(); ++i) {
        if (type == member_type(i))
          return i;
      }
      return -1;
    }

    /// \brief Check whether this union type contains the
    /// specified type.
    bool UnionType::contains_type(const ValuePtr<>& type) const {
      return index_of_type(type) >= 0;
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UnionValue, Constructor, union_v)
    
    UnionValue::UnionValue(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location)
    : Constructor(type, hashable_setup<UnionValue>(type)(value), location),
    m_value(value) {
      ValuePtr<UnionType> ty = dyn_cast<UnionType>(type);
      if (!ty)
        throw TvmUserError("first argument to union_v is not a union type");
      if (!ty->contains_type(value->type()))
        throw TvmUserError("second argument to union_v is not the a member of the result type");
    }
    
    ValuePtr<> UnionValue::get(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location) {
      return type->context().get_functional(UnionValue(type, value, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UnionElement, AggregateOp, union_el)
    
    UnionElement::UnionElement(const ValuePtr<>& aggregate, const ValuePtr<>& member_type, const SourceLocation& location)
    : AggregateOp(member_type, hashable_setup<UnionElement>(aggregate)(member_type), location),
    m_aggregate(aggregate),
    m_member_type(member_type) {
      ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(aggregate->type());
      if (!union_ty)
        throw TvmUserError("first argument to union_el must have union type");

      if (!union_ty->contains_type(member_type))
        throw TvmUserError("second argument to union_el is not a member of the type of the first");
    }
    
    ValuePtr<> UnionElement::get(const ValuePtr<>& aggregate, const ValuePtr<>& member_type, const SourceLocation& location) {
      return aggregate->context().get_functional(UnionElement(aggregate, member_type, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UnionElementPtr, AggregateOp, union_ep)

    UnionElementPtr::UnionElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type, const SourceLocation& location)
    : AggregateOp(member_type, hashable_setup<UnionElementPtr>(aggregate_ptr)(member_type), location),
    m_aggregate_ptr(aggregate_ptr),
    m_member_type(member_type) {
      ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(aggregate_ptr->type());
      if (!ptr_ty)
        throw TvmUserError("first argument to union_ep must have pointer type");

      ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(ptr_ty->target_type());
      if (!union_ty)
        throw TvmUserError("first argument to union_ep must have pointer to union type");

      if (!union_ty->contains_type(member_type))
        throw TvmUserError("second argument to union_el is not a member of the type of the first");
    }
    
    ValuePtr<> UnionElementPtr::get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type, const SourceLocation& location) {
      return aggregate_ptr->context().get_functional(UnionElementPtr(aggregate_ptr, member_type, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(FunctionSpecialize, FunctionalValue, specialize)
    
    namespace {
      ValuePtr<> function_specialize_type(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {
        ValuePtr<PointerType> target_ptr_type = dyn_cast<PointerType>(function->type());
        if (!target_ptr_type)
          throw TvmUserError("specialize target is not a function pointer");

        ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(target_ptr_type->target_type());
        if (!function_type)
          throw TvmUserError("specialize target is not a function pointer");

        if (parameters.size() > function_type->n_phantom())
          throw TvmUserError("Too many parameters given to specialize");

        std::vector<ValuePtr<> > apply_parameters(parameters);
        std::vector<ValuePtr<FunctionTypeParameter> > new_parameters;
        while (apply_parameters.size() < function_type->parameter_types().size()) {
          ValuePtr<FunctionTypeParameter> param =
            function_type->context().new_function_type_parameter(function_type->parameter_type_after(apply_parameters));
          apply_parameters.push_back(param);
          new_parameters.push_back(param);
        }

        ValuePtr<> result_type = function_type->result_type_after(apply_parameters);

        return function->context().get_function_type
          (function_type->calling_convention(),
           result_type,
           new_parameters,
           function_type->n_phantom() - parameters.size(),
           location);
      }
      
      HashableValueSetup function_specialize_hashable_setup(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters) {
        HashableValueSetup vh = hashable_setup<FunctionSpecialize>(function);
        for (std::vector<ValuePtr<> >::const_iterator ii = parameters.begin(), ie = parameters.end(); ii != ie; ++ii)
          vh.combine(*ii);
        return vh;
      }
    }
    
    FunctionSpecialize::FunctionSpecialize(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location)
    : FunctionalValue(function->context(), function_specialize_type(function, parameters, location),
                      function_specialize_hashable_setup(function, parameters), location),
    m_function(function),
    m_parameters(parameters) {
    }
    
    ValuePtr<> FunctionSpecialize::get(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {
      return function->context().get_functional(FunctionSpecialize(function, parameters, location));
    }
  }
}
