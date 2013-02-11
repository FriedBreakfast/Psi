#include "Aggregate.hpp"
#include "Number.hpp"
#include "Function.hpp"
#include "FunctionalBuilder.hpp"
#include "Utility.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    Metatype::Metatype(Context& context, const SourceLocation& location)
    : FunctionalValue(context, location) {
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

    ValuePtr<> MetatypeValue::check_type() const {
      ValuePtr<> intptr_type = FunctionalBuilder::size_type(context(), location());
      if (m_size->type() != intptr_type)
        throw TvmUserError("first parameter to type_v must be intptr");
      if (m_alignment->type() != intptr_type)
        throw TvmUserError("second parameter to type_v must be intptr");
      return FunctionalBuilder::type_type(context(), location());
    }
    
    template<typename V>
    void MetatypeValue::visit(V& v) {
      visit_base<Constructor>(v);
      v("size", &MetatypeValue::m_size)
      ("alignment", &MetatypeValue::m_alignment);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(MetatypeValue, Constructor, type_v)

    ValuePtr<> MetatypeSize::check_type() const {
      if (parameter()->type() != FunctionalBuilder::type_type(context(), location()))
        throw TvmUserError("Parameter to sizeof must be a type");
      return FunctionalBuilder::size_type(context(), location());
    }
    
    PSI_TVM_UNARY_OP_IMPL(MetatypeSize, UnaryOp, sizeof)

    ValuePtr<> MetatypeAlignment::check_type() const {
      if (parameter()->type() != FunctionalBuilder::type_type(context(), location()))
        throw TvmUserError("Parameter to sizeof must be a type");
      return FunctionalBuilder::size_type(context(), location());
    }
    
    PSI_TVM_UNARY_OP_IMPL(MetatypeAlignment, UnaryOp, alignof)

    EmptyType::EmptyType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    ValuePtr<> EmptyType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }
    
    template<typename V>
    void EmptyType::visit(V& v) {
      visit_base<Type>(v);
    }

    PSI_TVM_FUNCTIONAL_IMPL(EmptyType, Type, empty)

    EmptyValue::EmptyValue(Context& context, const SourceLocation& location)
    : Constructor(context, location) {
    }
    
    template<typename V>
    void EmptyValue::visit(V& v) {
      visit_base<Constructor>(v);
    }
    
    ValuePtr<> EmptyValue::check_type() const {
      return context().get_functional(EmptyType(context(), location()));
    }

    PSI_TVM_FUNCTIONAL_IMPL(EmptyValue, Constructor, empty_v)
    
    OuterPtr::OuterPtr(const ValuePtr<>& pointer, const SourceLocation& location)
    : AggregateOp(pointer->context(), location),
    m_pointer(pointer) {
    }

    template<typename V>
    void OuterPtr::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("pointer", &OuterPtr::m_pointer);
    }
    
    ValuePtr<> OuterPtr::check_type() const {
      ValuePtr<PointerType> ptr_type;
      if (!m_pointer || !(ptr_type = dyn_unrecurse<PointerType>(m_pointer->type())))
        throw TvmUserError("Parameter to outer_ptr is not a pointer");
      if (!ptr_type->upref())
        throw TvmUserError("Parameter to outer_ptr does not have a visible upward reference");
      
      if (ValuePtr<UpwardReference> up = dyn_unrecurse<UpwardReference>(ptr_type->upref()))
        return FunctionalBuilder::pointer_type(up->outer_type(), up->next(), location());
      else
        throw TvmInternalError("Unrecognised upward reference type");
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(OuterPtr, AggregateOp, outer_ptr);
    
    BlockType::BlockType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    ValuePtr<> BlockType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }
    
    template<typename V>
    void BlockType::visit(V& v) {
      visit_base<Type>(v);
    }

    PSI_TVM_FUNCTIONAL_IMPL(BlockType, Type, block)
    
    ByteType::ByteType(Context& context, const SourceLocation& location)
    : Type(context, location) {
    }
    
    template<typename V>
    void ByteType::visit(V& v) {
      visit_base<Type>(v);
    }
    
    ValuePtr<> ByteType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }

    PSI_TVM_FUNCTIONAL_IMPL(ByteType, Type, byte)
    
    ValuePtr<> UndefinedValue::check_type() const {
      if (!parameter()->is_type())
        throw TvmUserError("Argument to undef must be a type");
      return parameter();
    }
    
    PSI_TVM_UNARY_OP_IMPL(UndefinedValue, UnaryOp, undef);
    
    ValuePtr<> ZeroValue::check_type() const {
      if (!parameter()->is_type())
        throw TvmUserError("Argument to zero must be a type");
      return parameter();
    }
    
    PSI_TVM_UNARY_OP_IMPL(ZeroValue, UnaryOp, zero);
    
    PointerType::PointerType(const ValuePtr<>& target_type, const ValuePtr<>& upref, const SourceLocation& location)
    : Type(target_type->context(), location),
    m_target_type(target_type),
    m_upref(upref) {
    }
    
    template<typename V>
    void PointerType::visit(V& v) {
      visit_base<Type>(v);
      v("target_type", &PointerType::m_target_type)
      ("upref", &PointerType::m_upref);
    }
    
    ValuePtr<> PointerType::check_type() const {
      if (!m_target_type->is_type())
        throw TvmUserError("pointer argument must be a type");
      return FunctionalBuilder::type_type(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(PointerType, Type, pointer)
    
    UpwardReferenceType::UpwardReferenceType(Context& context, const SourceLocation& location)
    : FunctionalValue(context, location) {
    }

    ValuePtr<> UpwardReferenceType::check_type() const {
      return ValuePtr<>();
    }
    
    template<typename V>
    void UpwardReferenceType::visit(V& v) {
      visit_base<FunctionalValue>(v);
    }

    PSI_TVM_FUNCTIONAL_IMPL(UpwardReferenceType, FunctionalValue, upref_type)
    
    UpwardReference::UpwardReference(const ValuePtr<>& outer_type, const ValuePtr<>& index, const ValuePtr<>& next, const SourceLocation& location)
    : FunctionalValue(outer_type->context(), location),
    m_outer_type(outer_type),
    m_index(index),
    m_next(next) {
    }
    
    ValuePtr<> UpwardReference::check_type() const {
      return FunctionalBuilder::upref_type(context(), location());
    }
    
    template<typename V>
    void UpwardReference::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("outer_type", &UpwardReference::m_outer_type)
      ("index", &UpwardReference::m_index)
      ("next", &UpwardReference::m_next);
    }

    PSI_TVM_FUNCTIONAL_IMPL(UpwardReference, FunctionalValue, upref)
    
    ConstantType::ConstantType(const ValuePtr<>& value, const SourceLocation& location)
    : Type(value->context(), location),
    m_value(value) {
    }
    
    ValuePtr<> ConstantType::check_type() const {
      return FunctionalBuilder::type_type(context(), location());
    }
    
    template<typename V>
    void ConstantType::visit(V& v) {
      visit_base<Type>(v);
      v("value", &ConstantType::m_value);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ConstantType, Type, constant);
    
    PointerCast::PointerCast(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const ValuePtr<>& upref, const SourceLocation& location)
    : AggregateOp(pointer->context(), location),
    m_pointer(pointer),
    m_target_type(target_type),
    m_upref(upref) {
    }
    
    template<typename V>
    void PointerCast::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("pointer", &PointerCast::m_pointer)
      ("target_type", &PointerCast::m_target_type)
      ("upref", &PointerCast::m_upref);
    }
    
    ValuePtr<> PointerCast::check_type() const {
      if (!isa<PointerType>(m_pointer->type()))
        throw TvmUserError("first argument to cast is not a pointer");
      if (!m_target_type->is_type())
        throw TvmUserError("second argument to cast is not a type");
      return context().get_functional(PointerType(m_target_type, m_upref, location()));
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
    
    ArrayType::ArrayType(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location)
    : Type(element_type->context(), location),
    m_element_type(element_type),
    m_length(length) {
    }

    template<typename V>
    void ArrayType::visit(V& v) {
      visit_base<Type>(v);
      v("element_type", &ArrayType::m_element_type)
      ("length", &ArrayType::m_length);
    }
    
    ValuePtr<> ArrayType::check_type() const {
      if (m_length->type() != FunctionalBuilder::size_type(context(), location()))
        throw TvmUserError("Array length must be an intptr");
      return FunctionalBuilder::type_type(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayType, Type, array);
    
    ArrayValue::ArrayValue(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location)
    : Constructor(element_type->context(), location),
    m_element_type(element_type),
    m_elements(elements) {
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
      
      return FunctionalBuilder::array_type(m_element_type, FunctionalBuilder::size_value(m_element_type->context(), m_elements.size(), location()), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ArrayValue, Constructor, array_v)    
    
    StructType::StructType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Type(context, location),
    m_members(members) {
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
      return FunctionalBuilder::type_type(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructType, Type, struct)
    
    StructValue::StructValue(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Constructor(context, location),
    m_members(members) {
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
      return FunctionalBuilder::struct_type(context(), member_types, location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructValue, Constructor, struct_v)

    StructElementOffset::StructElementOffset(const ValuePtr<>& struct_type, unsigned index, const SourceLocation& location)
    : AggregateOp(struct_type->context(), location),
    m_struct_type(struct_type),
    m_index(index) {
    }

    template<typename V>
    void StructElementOffset::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("struct_type", &StructElementOffset::m_struct_type)
      ("index", &StructElementOffset::m_index);
    }
    
    ValuePtr<> StructElementOffset::check_type() const {
      ValuePtr<StructType> struct_ty = dyn_unrecurse<StructType>(m_struct_type);
      if (!struct_ty)
        throw TvmUserError("first argument to struct_eo is not a struct type");
      
      if (m_index >= struct_ty->n_members())
        throw TvmUserError("struct_eo member index out of range");
      return FunctionalBuilder::size_type(context(), location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(StructElementOffset, AggregateOp, struct_eo)

    UnionType::UnionType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location)
    : Type(context, location),
    m_members(members) {
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
      return FunctionalBuilder::type_type(context(), location());
    }
        
    PSI_TVM_FUNCTIONAL_IMPL(UnionType, Type, union)
    
    UnionValue::UnionValue(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location)
    : Constructor(type->context(), location),
    m_union_type(type),
    m_value(value) {
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

    ElementValue::ElementValue(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location)
    : AggregateOp(aggregate->context(), location),
    m_aggregate(aggregate),
    m_index(index) {
    }

    template<typename V>
    void ElementValue::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate", &ElementValue::m_aggregate)
      ("index", &ElementValue::m_index);
    }
    
    namespace {
      ValuePtr<> element_value_type(const Value& self, const ValuePtr<>& aggregate_type, const ValuePtr<>& index) {
        if (unrecurse(index->type()) != FunctionalBuilder::size_type(self.context(), self.location()))
          throw TvmUserError("element member index is not an intptr");
        
        ValuePtr<> unrec_aggregate = unrecurse(aggregate_type);
        if (ValuePtr<StructType> struct_ty = dyn_cast<StructType>(unrec_aggregate)) {
          unsigned idx = size_to_unsigned(index);
          if (idx < struct_ty->n_members())
            return struct_ty->member_type(idx);
          else
            throw TvmUserError("struct gep index out of range");
        } else if (ValuePtr<ArrayType> array_ty = dyn_cast<ArrayType>(unrec_aggregate)) {
          return array_ty->element_type();
        } else if (ValuePtr<UnionType> union_ty = dyn_cast<UnionType>(unrec_aggregate)) {
          unsigned idx = size_to_unsigned(index);
          if (idx < union_ty->n_members())
            return union_ty->member_type(idx);
          else
            throw TvmUserError("union gep index out of range");
        } else {
          throw TvmUserError("parameter to gep or element is not a recognised aggregate type");
        }
      }
    }
    
    ValuePtr<> ElementValue::check_type() const {
      return element_value_type(*this, m_aggregate->type(), m_index);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ElementValue, AggregateOp, element)
    
    ElementPtr::ElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location)
    : AggregateOp(aggregate_ptr->context(), location),
    m_aggregate_ptr(aggregate_ptr),
    m_index(index) {
    }

    template<typename V>
    void ElementPtr::visit(V& v) {
      visit_base<AggregateOp>(v);
      v("aggregate_ptr", &ElementPtr::m_aggregate_ptr)
      ("index", &ElementPtr::m_index);
    }
    
    ValuePtr<> ElementPtr::check_type() const {
      ValuePtr<PointerType> ptr_ty = dyn_unrecurse<PointerType>(m_aggregate_ptr->type());
      if (!ptr_ty)
        throw TvmUserError("First argument to gep is not a pointer");
      
      if (unrecurse(m_index->type()) != FunctionalBuilder::size_type(context(), location()))
        throw TvmUserError("second parameter to gep is not an intptr");

      return FunctionalBuilder::pointer_type(element_value_type(*this, ptr_ty->target_type(), m_index),
                                             FunctionalBuilder::upref(ptr_ty->target_type(), m_index, ptr_ty->upref(), location()),
                                             location());
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(ElementPtr, AggregateOp, gep)

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
      std::vector<ValuePtr<ParameterPlaceholder> > new_parameters;
      while (apply_parameters.size() < function_type->parameter_types().size()) {
        ValuePtr<> previous_type = function_type->parameter_type_after(apply_parameters);
        ValuePtr<ParameterPlaceholder> param =
          function_type->context().new_placeholder_parameter(previous_type, previous_type->location());
        apply_parameters.push_back(param);
        new_parameters.push_back(param);
      }

      ValuePtr<> result_type = function_type->result_type_after(apply_parameters);

      return m_function->context().get_function_type
        (function_type->calling_convention(),
          result_type,
          new_parameters,
          function_type->n_phantom() - m_parameters.size(),
          function_type->sret(),
          location());
    }

    template<typename V>
    void FunctionSpecialize::visit(V& v) {
      visit_base<FunctionalValue>(v);
      v("function", &FunctionSpecialize::m_function)
      ("parameters", &FunctionSpecialize::m_parameters);
    }
    
    PSI_TVM_FUNCTIONAL_IMPL(FunctionSpecialize, FunctionalValue, specialize)
  }
}
