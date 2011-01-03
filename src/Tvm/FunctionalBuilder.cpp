#include "FunctionalBuilder.hpp"
#include "Aggregate.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    /// \brief Get the metatype, the type of types.
    Term* FunctionalBuilder::type_type(Context& context) {
      return Metatype::get(context);
    }
    
    /// \brief Create a type just from its size and alignment
    Term* FunctionalBuilder::type_value(Term *size, Term *alignment) {
      return MetatypeValue::get(size, alignment);
    }
    
    /// \brief Get the size of a type.
    Term* FunctionalBuilder::type_size(Term *type) {
      if (MetatypeValue::Ptr mt = dyn_cast<MetatypeValue>(type)) {
        return mt->size();
      } else {
        return MetatypeSize::get(type);
      }
    }
    
    /// \brief Get the alignment of a type.
    Term* FunctionalBuilder::type_alignment(Term *type) {
      if (MetatypeValue::Ptr mt = dyn_cast<MetatypeValue>(type)) {
        return mt->alignment();
      } else {
        return MetatypeAlignment::get(type);
      }
    }

    /// \brief Get the type of blocks
    Term* FunctionalBuilder::block_type(Context& context) {
      return BlockType::get(context);
    }
    
    /// \brief Get the empty type
    Term* FunctionalBuilder::empty_type(Context& context) {
      return EmptyType::get(context);
    }
    
    /// \brief Get the unique value of the empty type
    Term* FunctionalBuilder::empty_value(Context& context) {
      return EmptyValue::get(context);
    }
    
    /**
     * \brief Get a the type of a pointer to a type.
     * 
     * \param target Type being pointed to.
     */
    Term* FunctionalBuilder::pointer_type(Term* target) {
      return PointerType::get(target);
    }
    
    /**
     * \brief Get an array type.
     * 
     * \param element_type The type of each element of the array.
     * 
     * \param length The array length.
     */
    Term* FunctionalBuilder::array_type(Term* element_type, Term* length) {
      return ArrayType::get(element_type, length);
    }
    
    /**
     * \brief Get a struct aggregate type.
     * 
     * \param context Present in case \c elements has zero length.
     * 
     * \param elements List of types of members of the struct.
     */
    Term* FunctionalBuilder::struct_type(Context& context, ArrayPtr<Term*const> elements) {
      return StructType::get(context, elements);
    }
    
    /**
     * \brief Get a union aggregate type.
     * 
     * \param context Present in case \c elements has zero length.
     * 
     * \param elements List of types of members of the union.
     */
    Term* FunctionalBuilder::union_type(Context& context, ArrayPtr<Term*const> elements) {
      return UnionType::get(context, elements);
    }
    
    /**
     * Construct an array value.
     * 
     * \param element_type Type of array elements. Present in case
     * \c elements has zero length.
     * 
     * \param elements Values of array elements.
     */
    Term* FunctionalBuilder::array_value(Term* element_type, ArrayPtr<Term*const> elements) {
      return ArrayValue::get(element_type, elements);
    }
    
    /**
     * Construct a struct value.
     * 
     * \param context Context, in case \c elements has zero length.
     * 
     * \param elements Values of structure elements. The structure
     * type will be inferred from the types of these elements.
     */
    Term* FunctionalBuilder::struct_value(Context& context, ArrayPtr<Term*const> elements) {
      return StructValue::get(context, elements);
    }
    
    /**
     * Construct a union value.
     * 
     * The index into the union is not specified since different union
     * elements could potentially have the same type, and not specifying
     * an index means this can be recognised.
     * 
     * \param type Type of union to create a value for. <tt>value->type()</tt>
     * must be an element of this union type.
     * 
     * \param value Value for an element of the union.
     */
    Term* FunctionalBuilder::union_value(Term* type, Term* value) {
      return UnionValue::get(cast<UnionType>(type), value);
    }
    
    /**
     * \brief Get the value of an array element.
     * 
     * Although indexing by an entirely dynamic index is supported
     * (so for instance an array could be looped over) it should not
     * be done - the reason the index is not a constant is so that
     * expressions involving constants such as the array length, which
     * may not be locally known, are supported. Loops should use arrays
     * on the heap.
     * 
     * \param array Array being subscripted.
     * 
     * \param index Index into the array.
     */
    Term* FunctionalBuilder::array_element(Term* array, Term* index) {
      Term *result = ArrayElement::get(array, index);
      
      if (ArrayValue::Ptr array_val = dyn_cast<ArrayValue>(array)) {
        if (IntegerValue::Ptr index_val = dyn_cast<IntegerValue>(index)) {
          boost::optional<unsigned> index_ui = index_val->value_unsigned();
          if (index_ui && (*index_ui < array_val->length()))
            return array_val->value(*index_ui);
          else
            throw TvmUserError("array index out of range");
        }
      }
      
      return result;
    }
    
    /**
     * \brief Get the value of an array element.
     * 
     * \param array Array being subscripted.
     * \param index Index into the array.
     */
    Term* FunctionalBuilder::array_element(Term* array, unsigned index) {
      return array_element(array, int_value(size_type(array->context()), index));
    }
    
    /**
     * \brief Get the value of a struct member.
     * 
     * \param aggregate Struct being subscripted.
     * \param index Index of the member to get a value for.
     */
    Term* FunctionalBuilder::struct_element(Term* aggregate, unsigned index) {
      Term *result = StructElement::get(aggregate, index);
      
      if (StructValue::Ptr struct_val = dyn_cast<StructValue>(aggregate)) {
        return struct_val->member_value(index);
      }
      
      return result;
    }
    
    /**
     * \brief Get the value of a union member.
     * 
     * \param aggregate Union being subscripted.
     * \param member_type Type of the member whose value is returned.
     */
    Term* FunctionalBuilder::union_element(Term* aggregate, Term* member_type) {
      Term *result = UnionElement::get(aggregate, member_type);
      
      if (UnionValue::Ptr union_val = dyn_cast<UnionValue>(aggregate)) {
        Term *value = union_val->value();
        if (member_type == value->type())
          return value;
      }
      
      return result;
    }
    
    /**
     * \brief Get the value of a struct member.
     * 
     * This version of the function translates the index into a type in
     * constructing the operation. Different members with the same type
     * will therefore be considered equivalent.
     * 
     * \param aggregate Union being subscripted.
     * \param index Index of the member to get a value for.
     */
    Term* FunctionalBuilder::union_element(Term* aggregate, unsigned index) {
      UnionType::Ptr union_ty = dyn_cast<UnionType>(aggregate->type());
      if (!union_ty)
        throw TvmUserError("union_el aggregate parameter is not a union");
      if (index >= union_ty->n_members())
        throw TvmUserError("union member index out of range");
      return union_element(aggregate, union_ty->member_type(index));
    }
    
    /**
     * \brief Get a pointer to an array element.
     * 
     * \param aggregate Pointer to an array.
     * 
     * \param index Index of element to get.
     */
    Term* FunctionalBuilder::array_element_ptr(Term *array, Term *index) {
      return ArrayElementPtr::get(array, index);
    }
    
    /**
     * \brief Get a pointer to an array element.
     * 
     * \param aggregate Pointer to an array.
     * 
     * \param index Index of element to get.
     */
    Term* FunctionalBuilder::array_element_ptr(Term *array, unsigned index) {
      return array_element_ptr(array, int_value(size_type(array->context()), index));
    }
    
    /**
     * \brief Get a pointer to a struct member.
     * 
     * \param aggregate Pointer to a struct.
     * 
     * \param index Index of member to get a pointer to.
     */
    Term* FunctionalBuilder::struct_element_ptr(Term *aggregate, unsigned index) {
      return StructElementPtr::get(aggregate, index);
    }
    
    /**
     * \brief Get a pointer to a union member.
     * 
     * \param aggregate Pointer to a union.
     * 
     * \param type Member type to get a pointer to.
     */
    Term* FunctionalBuilder::union_element_ptr(Term *aggregate, Term *type) {
      return UnionElementPtr::get(aggregate, type);
    }
    
    /**
     * \brief Get a pointer to a union member.
     * 
     * This looks up the type of the member specified and forwards to
     * union_element_ptr(Term*,Term*).
     * 
     * \param aggregate Pointer to a union.
     * 
     * \param index Index of member to get a pointer to.
     */
    Term* FunctionalBuilder::union_element_ptr(Term *aggregate, unsigned index) {
      PointerType::Ptr union_ptr_ty = dyn_cast<PointerType>(aggregate->type());
      if (!union_ptr_ty)
        throw TvmUserError("union_ep aggregate parameter is not a pointer");
      UnionType::Ptr union_ty = dyn_cast<UnionType>(union_ptr_ty->target_type());
      if (!union_ty)
        throw TvmUserError("union_ep aggregate parameter is not a pointer to a union");
      if (index >= union_ty->n_members())
        throw TvmUserError("union member index out of range");
      return union_element_ptr(aggregate, union_ty->member_type(index));
    }
    
    /**
     * \brief Cast a pointer from one type to another.
     * 
     * \param ptr Original pointer.
     * 
     * \param result_type Type of pointed-to type of the new pointer (and
     * hence not necessarily a pointer type itself).
     */
    Term* FunctionalBuilder::pointer_cast(Term *ptr, Term *result_type) {
      // Try to get to the lowest pointer if multiple casts are involved
      while (true) {
        PointerCast::Ptr cast_ptr = dyn_cast<PointerCast>(ptr);
        if (cast_ptr)
          ptr = cast_ptr->pointer();
        else
          break;
      }
      return PointerCast::get(ptr, result_type);
    }
    
    /**
     * \brief Get a pointer which is at a specified offset from an existing pointer.
     * 
     * \param ptr Original pointer.
     * 
     * \param offset Offset from original pointer in units of the
     * pointed-to type.
     */
    Term* FunctionalBuilder::pointer_offset(Term *ptr, Term *offset) {
      return PointerOffset::get(ptr, offset);
    }
    
    /// \brief Get an integer type
    Term* FunctionalBuilder::int_type(Context& context, IntegerType::Width width, bool is_signed) {
      return IntegerType::get(context, width, is_signed);
    }
    
    /// \brief Get the intptr type
    Term* FunctionalBuilder::size_type(Context& context) {
      return int_type(context, IntegerType::iptr, false);
    }
    
    /**
     * \brief Get a constant integer value.
     *
     * This should only be used for small, known constants,
     * otherwise just construct a term representing whatever
     * arithmetic would be used to calculate the value.
     * 
     * \param type Integer type.
     * 
     * \param value Value of the integer to get.
     */    
    Term* FunctionalBuilder::int_value(Term *type, int value) {
      return IntegerValue::get(type, IntegerValue::convert(value));
    }

    /// \copydoc FunctionalBuilder::int_value(Term*,int)
    Term* FunctionalBuilder::int_value(Term *type, unsigned value) {
      return IntegerValue::get(type, IntegerValue::convert(value));
    }

    /**
     * Parse an integer value and return and integer constant.
     * 
     * The actual parsing is done by IntegerValue::parse.
     * 
     * \param value Integer value to parse. This should not have
     * any leading base prefix or minus sign. Base and sign are
     * specified using other parameters to this function.
     * 
     * \param negative Whether the value computed from \c value
     * should be multiplied by -1 to make the integer value.
     * 
     * \param base Base to use to parse the string.
     */
    Term* FunctionalBuilder::int_value(Term* type, const std::string& value, bool negative, unsigned base) {
      return IntegerValue::get(type, IntegerValue::parse(value, negative, base));
    }

    /// \brief Get an integer add operation.
    Term *FunctionalBuilder::add(Term *lhs, Term *rhs) {
      return IntegerAdd::get(lhs, rhs);
    }
    
    /// \brief Get an integer subtract operation.
    Term *FunctionalBuilder::sub(Term *lhs, Term *rhs) {
      return IntegerSubtract::get(lhs, rhs);
    }
    
    /// \brief Get an integer multiply operation.
    Term *FunctionalBuilder::mul(Term *lhs, Term *rhs) {
      return IntegerMultiply::get(lhs, rhs);
    }
    
    /// \brief Get an integer division operation.
    Term *FunctionalBuilder::div(Term *lhs, Term *rhs) {
      return IntegerDivide::get(lhs, rhs);
    }
    
    /// \brief Get an integer negation operation
    Term *FunctionalBuilder::neg(Term *parameter) {
      return IntegerNegative::get(parameter);
    }
    
    /// \brief Get a bitwise and operation
    Term *FunctionalBuilder::bit_and(Term *lhs, Term *rhs) {
      return BitAnd::get(lhs, rhs);
    }
    
    /// \brief Get a bitwise or operation
    Term *FunctionalBuilder::bit_or(Term *lhs, Term *rhs) {
      return BitOr::get(lhs, rhs);
    }
    
    /// \brief Get a bitwise exclusive or operation
    Term *FunctionalBuilder::bit_xor(Term *lhs, Term *rhs) {
      return BitXor::get(lhs, rhs);
    }
    
    /// \brief Get a bitwise inverse operation
    Term *FunctionalBuilder::bit_not(Term *parameter) {
      return BitNot::get(parameter);
    }

    /// \brief Get an integer == comparison operation
    Term *FunctionalBuilder::cmp_eq(Term *lhs, Term *rhs) {
      return IntegerCompareEq::get(lhs, rhs);
    }
    
    /// \brief Get an integer != comparison operation
    Term *FunctionalBuilder::cmp_ne(Term *lhs, Term *rhs) {
      return IntegerCompareNe::get(lhs, rhs);
    }
    
    /// \brief Get an integer \> comparison operation
    Term *FunctionalBuilder::cmp_gt(Term *lhs, Term *rhs) {
      return IntegerCompareGt::get(lhs, rhs);
    }
    
    /// \brief Get an integer \> comparison operation
    Term *FunctionalBuilder::cmp_ge(Term *lhs, Term *rhs) {
      return IntegerCompareGe::get(lhs, rhs);
    }
    
    /// \brief Get an integer \< comparison operation
    Term *FunctionalBuilder::cmp_lt(Term *lhs, Term *rhs) {
      return IntegerCompareLt::get(lhs, rhs);
    }
    
    /// \brief Get an integer \<= comparison operation
    Term *FunctionalBuilder::cmp_le(Term *lhs, Term *rhs) {
      return IntegerCompareLe::get(lhs, rhs);
    }

    /// \brief Get the maximum of two integers
    Term *FunctionalBuilder::max(Term *lhs, Term *rhs) {
      Term *cond = cmp_ge(lhs, rhs);
      return select(cond, lhs, rhs);
    }
    
    /// \brief Get the minimum of two integers
    Term *FunctionalBuilder::min(Term *lhs, Term *rhs) {
      Term *cond = cmp_le(lhs, rhs);
      return select(cond, lhs, rhs);
    }

    /**
     * \brief Align an offset to a specified alignment, which must be a
     * power of two.
     *
     * The formula used is: <tt>(offset + align - 1) & ~(align - 1)</tt>
     */
    Term* FunctionalBuilder::align_to(Term *offset, Term *align) {
      Context& context = offset->context();
      Term *one = int_value(size_type(context), 1);
      Term *align_minus_one = sub(align, one);
      Term *offset_plus_align_minus_one = add(offset, align_minus_one);
      Term *not_align_minus_one = bit_not(align_minus_one);
      return bit_and(offset_plus_align_minus_one, not_align_minus_one);
    }
  
    /**
     * \brief Get a select operation.
     * 
     * \param condition Condition to use to decide which value is returned.
     * 
     * \param if_true Value of this operation if \c condition is true.
     * 
     * \param if_false Value of this operation if \c condition is false.
     */
    Term *FunctionalBuilder::select(Term *condition, Term *if_true, Term *if_false) {
      return SelectValue::get(condition, if_true, if_false);
    }
  }
}