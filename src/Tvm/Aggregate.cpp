#include "Aggregate.hpp"
#include "Number.hpp"
#include "Function.hpp"
#include "FunctionalBuilder.hpp"
#include "Utility.hpp"
#include <boost/concept_check.hpp>
#include <boost/concept_check.hpp>

namespace Psi {
  namespace Tvm {
    Metatype::Metatype(Context& context, const SourceLocation& location)
    : FunctionalValue(context, location) {
    }
    
    ValuePtr<> Metatype::get(Context& context, const SourceLocation& location) {
      return context.get_functional(Metatype(context, location));
    }
    
    ValuePtr<> Metatype::check_type() const {
      PSI_ASSERT(category() == category_undetermined);
      return ValuePtr<>();
    }
    
    template<typename V>
    void Metatype::visit(V& v) {
      visit_base<FunctionalValue>(v);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(Metatype, FunctionalValue, type)
    
    MetatypeValue::MetatypeValue(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location)
    : Constructor(size->context(), location),
    m_size(size),
    m_alignment(alignment) {
    }

    ValuePtr<> MetatypeValue::get(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location) {
      return size->context().get_functional(MetatypeValue(size, alignment, location));
    }
    
    ValuePtr<> MetatypeValue::check_type() const {
      ValuePtr<> intptr_type = IntegerType::get_intptr(context(), location());
      if (m_size->type() != intptr_type)
        throw TvmUserError("first parameter to type_v must be intptr");
      if (m_alignment->type() != intptr_type)
        throw TvmUserError("second parameter to type_v must be intptr");
      return Metatype::get(context(), location());
    }
    
    template<typename V>
    void MetatypeValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("size", &MetatypeValue::m_size)
      ("alignment", &MetatypeValue::m_alignment);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(MetatypeValue, Constructor, type_v)

    ValuePtr<> MetatypeSize::check_type() const {
      if (parameter()->type() != Metatype::get(context(), location()))
        throw TvmUserError("Parameter to sizeof must be a type");
      return IntegerType::get_intptr(context(), location());
    }
    
    PSI_TVM_UNARY_OP_IMPL(MetatypeSize, UnaryOp, sizeof)

    ValuePtr<> MetatypeAlignment::check_type() const {
      if (parameter()->type() != Metatype::get(context(), location()))
        throw TvmUserError("Parameter to sizeof must be a type");
      return IntegerType::get_intptr(context(), location());
    }
    
    PSI_TVM_UNARY_OP_IMPL(MetatypeAlignment, UnaryOp, alignof)

    EmptyType::EmptyType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    ValuePtr<> EmptyType::check_type() const {
      return Metatype::get(context(), location());
    }
    
    template<typename V>
    void EmptyType::visit(V& v) {
      visit_base<Type>(v);
    }

    ValuePtr<EmptyType> EmptyType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(EmptyType(context, location));
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(EmptyType, Type, empty)

    EmptyValue::EmptyValue(Context& context, const SourceLocation& location)
    : Constructor(context, location) {
    }
    
    ValuePtr<EmptyValue> EmptyValue::get(Context& context, const SourceLocation& location) {
      return context.get_functional(EmptyValue(context, location));
    }
    
    template<typename V>
    void EmptyValue::visit(V& v) {
      visit_base<Constructor>(v);
    }
    
    ValuePtr<> EmptyValue::check_type() const {
      return EmptyType::get(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(EmptyValue, Constructor, empty_v)
    
    BlockType::BlockType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    ValuePtr<> BlockType::check_type() const {
      return Metatype::get(context(), location());
    }
    
    template<typename V>
    void BlockType::visit(V& v) {
      visit_base<Type>(v);
    }

    ValuePtr<BlockType> BlockType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(BlockType(context, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(BlockType, Type, block)
    
    ByteType::ByteType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    ValuePtr<ByteType> ByteType::get(Context& context, const SourceLocation& location) {
      return context.get_functional(ByteType(context, location));
    }
    
    template<typename V>
    void ByteType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    ValuePtr<> ByteType::check_type() const {
      return Metatype::get(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(ByteType, Type, byte)
    
    ValuePtr<> UndefinedValue::check_type() const {
      if (!parameter()->is_type())
        throw TvmUserError("Argument to undef must be a type");
      return parameter();
    }
    
    PSI_TVM_UNARY_OP_IMPL(UndefinedValue, UnaryOp, undef);
    
    PointerType::PointerType(const ValuePtr<>& target_type, const SourceLocation& location)
    : Type(target_type->context(), location),
    m_target_type(target_type) {
    }
    
    ValuePtr<> PointerType::get(const ValuePtr<>& target_type, const SourceLocation& location) {
      return target_type->context().get_functional(PointerType(target_type, location));
    }
    
    template<typename V>
    void PointerType::visit(V& v) {
      visit_base<Type>(v);
      v("target_type", &PointerType::m_target_type);
    }
    
    ValuePtr<> PointerType::check_type() const {
      if (!m_target_type->is_type())
        throw TvmUserError("pointer argument must be a type");
      return Metatype::get(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(PointerType, Type, pointer)
    
    PointerCast::PointerCast(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const SourceLocation& location)
    : AggregateOp(pointer->context(), location),
    m_pointer(pointer),
    m_target_type(target_type) {
    }
    
    ValuePtr<> PointerCast::get(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const SourceLocation& location) {
      return pointer->context().get_functional(PointerCast(pointer, target_type, location));
    }
    
    template<typename V>
    void PointerCast::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("pointer", &PointerCast::m_pointer)
      ("target_type", &PointerCast::m_target_type);
    }
    
    ValuePtr<> PointerCast::check_type() const {
      if (!isa<PointerType>(m_pointer->type()))
        throw TvmUserError("first argument to cast is not a pointer");
      if (!m_target_type->is_type())
        throw TvmUserError("second argument to cast is not a type");
      return PointerType::get(m_target_type, location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(PointerCast, AggregateOp, cast)
    
    PointerOffset::PointerOffset(const ValuePtr<>& pointer, const ValuePtr<>& offset, const SourceLocation& location)
    : AggregateOp(pointer->context(), location),
    m_pointer(pointer),
    m_offset(offset) {
    }

    template<typename V>
    void PointerOffset::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("pointer", &PointerOffset::m_pointer)
      ("offset", &PointerOffset::m_offset);
    }
    
    ValuePtr<> PointerOffset::check_type() const {
      if (!isa<PointerType>(m_pointer->type()))
        throw TvmUserError("first argument to offset is not a pointer");
      ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(m_offset->type());
      if (!int_ty || (int_ty->width() != IntegerType::iptr))
        throw TvmUserError("second argument to offset is not an intptr or uintptr");
      return m_pointer->type();
    }

    PSI_TVM_FUNCTIONAL_IMPL(PointerOffset, AggregateOp, offset)
    
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
    
    ArrayType::ArrayType(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location)
    : Type(element_type->context(), location),
    m_element_type(element_type),
    m_length(length) {
    }
    
    ValuePtr<> ArrayType::get(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location) {
      return element_type->context().get_functional(ArrayType(element_type, length, location));
    }

    template<typename V>
    void ArrayType::visit(V& v) {
      visit_base<Type>(v);
      v("element_type", &ArrayType::m_element_type)
      ("length", &ArrayType::m_length);
    }
    
    ValuePtr<> ArrayType::check_type() const {
      if (m_length->type() != IntegerType::get_intptr(context(), location()))
        throw TvmUserError("Array length must be an intptr");
      return Metatype::get(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayType, Type, array);
    
    ArrayValue::ArrayValue(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location)
    : Constructor(element_type->context(), location),
    m_element_type(element_type),
    m_elements(elements) {
    }

    ValuePtr<> ArrayValue::get(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location) {
      return element_type->context().get_functional(ArrayValue(element_type, elements, location));
    }
    
    template<typename V>
    void ArrayValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("element_type", &ArrayValue::m_element_type)
      ("elements", &ArrayValue::m_elements);
    }
    
    ValuePtr<> ArrayValue::check_type() const {
      if (!m_element_type->is_type())
        throw TvmUserError("first argument to array value is not a type");

      for (std::vector<ValuePtr<> >::const_iterator ii = m_elements.begin(), ie = m_elements.end(); ii != ie; ++ii) {
        if ((*ii)->type() != m_element_type)
          throw TvmUserError("array value element is of the wrong type");
      }
      
      return ArrayType::get(m_element_type, IntegerValue::get_intptr(m_element_type->context(), m_elements.size(), location()), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayValue, Constructor, array_v)
    
    ArrayElement::ArrayElement(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location)
    : AggregateOp(aggregate->context(), location),
    m_aggregate(aggregate),
    m_index(index) {
    }

    ValuePtr<> ArrayElement::get(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location) {
      return aggregate->context().get_functional(ArrayElement(aggregate, index, location));
    }
    
    template<typename V>
    void ArrayElement::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate", &ArrayElement::m_aggregate)
      ("index", &ArrayElement::m_index);
    }
    
    ValuePtr<> ArrayElement::check_type() const {
      if (m_index->type() != IntegerType::get_intptr(context(), location()))
        throw TvmUserError("second parameter to array_el is not an intptr");
      ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(m_aggregate->type());
      if (!array_ty)
        throw TvmUserError("first parameter to array_el is not an array");
      return array_ty->element_type();
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayElement, AggregateOp, array_el)
    
    ArrayElementPtr::ArrayElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location)
    : AggregateOp(aggregate_ptr->context(), location),
    m_aggregate_ptr(aggregate_ptr),
    m_index(index) {
    }

    ValuePtr<> ArrayElementPtr::get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location) {
      return aggregate_ptr->context().get_functional(ArrayElementPtr(aggregate_ptr, index, location));
    }
    
    template<typename V>
    void ArrayElementPtr::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate_ptr", &ArrayElementPtr::m_aggregate_ptr)
      ("index", &ArrayElementPtr::m_index);
    }
    
    ValuePtr<> ArrayElementPtr::check_type() const {
      if (m_index->type() != IntegerType::get_intptr(context(), location()))
        throw TvmUserError("second parameter to array_ep is not an intptr");

      if (ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(m_aggregate_ptr->type()))
        if (ValuePtr<ArrayType> ty = dyn_cast<ArrayType>(ptr_ty->target_type()))
          return PointerType::get(ty->element_type(), location());

      throw TvmUserError("first parameter to array_ep does not have pointer to array type");
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayElementPtr, AggregateOp, array_ep)
    
    StructType::StructType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Type(context, location),
    m_members(members) {
    }

    ValuePtr<StructType> StructType::get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location) {
      return context.get_functional(StructType(context, members, location));
    }

    template<typename V>
    void StructType::visit(V& v) {
      visit_base<Type>(v);
      v("members", &StructType::m_members);
    }
    
    ValuePtr<> StructType::check_type() const {
      for (std::vector<ValuePtr<> >::const_iterator ii = m_members.begin(), ie = m_members.end(); ii != ie; ++ii) {
        if (!(*ii)->is_type())
          throw TvmUserError("struct argument is not a type");
      }
      return Metatype::get(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructType, Type, struct)
    
    StructValue::StructValue(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Constructor(context, location),
    m_members(members) {
    }

    ValuePtr<> StructValue::get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location) {
      return context.get_functional(StructValue(context, members, location));
    }
    
    template<typename V>
    void StructValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("members", &StructValue::m_members);
    }
    
    ValuePtr<> StructValue::check_type() const {
      std::vector<ValuePtr<> > member_types;
      for (std::vector<ValuePtr<> >::const_iterator ii = m_members.begin(), ie = m_members.end(); ii != ie; ++ii)
        member_types.push_back((*ii)->type());
      return StructType::get(context(), member_types, location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructValue, Constructor, struct_v)
    
    StructElement::StructElement(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location)
    : AggregateOp(aggregate->context(), location),
    m_aggregate(aggregate),
    m_index(index) {
    }

    ValuePtr<> StructElement::get(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location) {
      return aggregate->context().get_functional(StructElement(aggregate, index, location));
    }
    
    template<typename V>
    void StructElement::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate", &StructElement::m_aggregate)
      ("index", &StructElement::m_index);
    }
    
    ValuePtr<> StructElement::check_type() const {
      ValuePtr<StructType> ty = dyn_cast<StructType>(m_aggregate->type());
      if (!ty)
        throw TvmUserError("parameter to struct_el does not have struct type");
      if (m_index >= ty->n_members())
        throw TvmUserError("struct_el member index out of range");
      return ty->member_type(m_index);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElement, AggregateOp, struct_el)
    
    StructElementPtr::StructElementPtr(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location)
    : AggregateOp(aggregate_ptr->context(), location),
    m_aggregate_ptr(aggregate_ptr),
    m_index(index) {
    }

    ValuePtr<> StructElementPtr::get(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location) {
      return aggregate_ptr->context().get_functional(StructElementPtr(aggregate_ptr, index, location));
    }
    
    template<typename V>
    void StructElementPtr::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate_ptr", &StructElementPtr::m_aggregate_ptr)
      ("index", &StructElementPtr::m_index);
    }
    
    ValuePtr<> StructElementPtr::check_type() const {
      ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(m_aggregate_ptr->type());
      if (!ptr_ty)
        throw TvmUserError("first argument to struct_ep is not a pointer");
      
      ValuePtr<StructType> ty = dyn_cast<StructType>(ptr_ty->target_type());
      if (!ty)
        throw TvmUserError("first argument to struct_ep is not a pointer to a struct");
      
      if (m_index >= ty->n_members())
        throw TvmUserError("struct_el member index out of range");
      return PointerType::get(ty->member_type(m_index), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElementPtr, AggregateOp, struct_ep)
    
    StructElementOffset::StructElementOffset(const ValuePtr<>& aggregate_type, unsigned index, const SourceLocation& location)
    : AggregateOp(aggregate_type->context(), location),
    m_aggregate_type(aggregate_type),
    m_index(index) {
    }
    
    ValuePtr<> StructElementOffset::get(const ValuePtr<>& aggregate_type, unsigned index, const SourceLocation& location) {
      return aggregate_type->context().get_functional(StructElementOffset(aggregate_type, index, location));
    }

    template<typename V>
    void StructElementOffset::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate_type", &StructElementOffset::m_aggregate_type)
      ("index", &StructElementOffset::m_index);
    }
    
    ValuePtr<> StructElementOffset::check_type() const {
      ValuePtr<StructType> struct_ty = dyn_cast<StructType>(m_aggregate_type);
      if (!struct_ty)
        throw TvmUserError("first argument to struct_eo is not a struct type");
      if (m_index >= struct_ty->n_members())
        throw TvmUserError("struct_eo member index out of range");
      return IntegerType::get_intptr(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElementOffset, AggregateOp, struct_eo)

    UnionType::UnionType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Type(context, location),
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
    
    template<typename V>
    void UnionType::visit(V& v) {
      visit_base<Type>(v);
      v("members", &UnionType::m_members);
    }
    
    ValuePtr<> UnionType::check_type() const {
      for (std::vector<ValuePtr<> >::const_iterator ii = m_members.begin(), ie = m_members.end(); ii != ie; ++ii) {
        if (!(*ii)->is_type())
          throw TvmUserError("union argument is not a type");
      }
      return Metatype::get(context(), location());
    }
        
    PSI_TVM_FUNCTIONAL_IMPL(UnionType, Type, union)
    
    UnionValue::UnionValue(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location)
    : Constructor(type->context(), location),
    m_union_type(type),
    m_value(value) {
    }
    
    ValuePtr<> UnionValue::get(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location) {
      return type->context().get_functional(UnionValue(type, value, location));
    }
    
    template<typename V>
    void UnionValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("value", &UnionValue::m_value);
    }
    
    ValuePtr<> UnionValue::check_type() const {
      ValuePtr<UnionType> ty = dyn_cast<UnionType>(m_union_type);
      if (!ty)
        throw TvmUserError("first argument to union_v is not a union type");
      if (!ty->contains_type(m_value->type()))
        throw TvmUserError("second argument to union_v is not the a member of the result type");
      return m_union_type;
    }
        
    PSI_TVM_FUNCTIONAL_IMPL(UnionValue, Constructor, union_v)
    
    UnionElement::UnionElement(const ValuePtr<>& aggregate, const ValuePtr<>& member_type, const SourceLocation& location)
    : AggregateOp(aggregate->context(), location),
    m_aggregate(aggregate),
    m_member_type(member_type) {
    }
    
    ValuePtr<> UnionElement::get(const ValuePtr<>& aggregate, const ValuePtr<>& member_type, const SourceLocation& location) {
      return aggregate->context().get_functional(UnionElement(aggregate, member_type, location));
    }
    
    template<typename V>
    void UnionElement::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate", &UnionElement::m_aggregate)
      ("member_type", &UnionElement::m_member_type);
    }
    
    ValuePtr<> UnionElement::check_type() const {
      ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(m_aggregate->type());
      if (!union_ty)
        throw TvmUserError("first argument to union_el must have union type");

      if (!union_ty->contains_type(m_member_type))
        throw TvmUserError("second argument to union_el is not a member of the type of the first");
      
      return m_member_type;
    }
        
    PSI_TVM_FUNCTIONAL_IMPL(UnionElement, AggregateOp, union_el)

    UnionElementPtr::UnionElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type, const SourceLocation& location)
    : AggregateOp(aggregate_ptr->context(), location),
    m_aggregate_ptr(aggregate_ptr),
    m_member_type(member_type) {
    }
    
    ValuePtr<> UnionElementPtr::get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type, const SourceLocation& location) {
      return aggregate_ptr->context().get_functional(UnionElementPtr(aggregate_ptr, member_type, location));
    }
    
    template<typename V>
    void UnionElementPtr::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate_ptr", &UnionElementPtr::m_aggregate_ptr)
      ("member_type", &UnionElementPtr::m_member_type);
    }
    
    ValuePtr<> UnionElementPtr::check_type() const {
      ValuePtr<PointerType> ptr_ty = dyn_cast<PointerType>(m_aggregate_ptr->type());
      if (!ptr_ty)
        throw TvmUserError("first argument to union_ep must have pointer type");

      ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(ptr_ty->target_type());
      if (!union_ty)
        throw TvmUserError("first argument to union_ep must have pointer to union type");

      if (!union_ty->contains_type(m_member_type))
        throw TvmUserError("second argument to union_el is not a member of the type of the first");
      
      return PointerType::get(m_member_type, location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(UnionElementPtr, AggregateOp, union_ep)
    
    FunctionSpecialize::FunctionSpecialize(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location)
    : FunctionalValue(function->context(), location),
    m_function(function),
    m_parameters(parameters) {
    }
    
    ValuePtr<> FunctionSpecialize::check_type() const {
      ValuePtr<PointerType> target_ptr_type = dyn_cast<PointerType>(m_function->type());
      if (!target_ptr_type)
        throw TvmUserError("specialize target is not a function pointer");

      ValuePtr<FunctionType> function_type = dyn_cast<FunctionType>(target_ptr_type->target_type());
      if (!function_type)
        throw TvmUserError("specialize target is not a function pointer");

      if (m_parameters.size() > function_type->n_phantom())
        throw TvmUserError("Too many parameters given to specialize");

      std::vector<ValuePtr<> > apply_parameters(m_parameters);
      std::vector<ValuePtr<FunctionTypeParameter> > new_parameters;
      while (apply_parameters.size() < function_type->parameter_types().size()) {
        ValuePtr<> previous_type = function_type->parameter_type_after(apply_parameters);
        ValuePtr<FunctionTypeParameter> param =
          function_type->context().new_function_type_parameter(previous_type, previous_type->location());
        apply_parameters.push_back(param);
        new_parameters.push_back(param);
      }

      ValuePtr<> result_type = function_type->result_type_after(apply_parameters);

      return m_function->context().get_function_type
        (function_type->calling_convention(),
          result_type,
          new_parameters,
          function_type->n_phantom() - m_parameters.size(),
          location());
    }

    template<typename V>
    void FunctionSpecialize::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("function", &FunctionSpecialize::m_function)
      ("parameters", &FunctionSpecialize::m_parameters);
    }
    
    ValuePtr<> FunctionSpecialize::get(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {
      return function->context().get_functional(FunctionSpecialize(function, parameters, location));
    }

    PSI_TVM_FUNCTIONAL_IMPL(FunctionSpecialize, FunctionalValue, specialize)
  }
}
