#include "FunctionalBuilder.hpp"
#include "Aggregate.hpp"
#include "Number.hpp"
#include "Function.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    /// \brief Get the metatype, the type of types.
    ValuePtr<> FunctionalBuilder::type_type(Context& context, const SourceLocation& location) {
      return context.get_functional(Metatype(context, location));
    }
    
    /// \brief Create a type just from its size and alignment
    ValuePtr<> FunctionalBuilder::type_value(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location) {
      return size->context().get_functional(MetatypeValue(size, alignment, location));
    }
    
    /// \brief Get the size of a type.
    ValuePtr<> FunctionalBuilder::type_size(const ValuePtr<>& type, const SourceLocation& location) {
      if (ValuePtr<MetatypeValue> mt = dyn_cast<MetatypeValue>(type)) {
        return mt->size();
      }
      
      const ValuePtr<>& result = type->context().get_functional(MetatypeSize(type, location));
      
      if (isa<UndefinedValue>(type))
        return undef(result->type(), location);
      
      return result;
    }
    
    /// \brief Get the alignment of a type.
    ValuePtr<> FunctionalBuilder::type_alignment(const ValuePtr<>& type, const SourceLocation& location) {
      if (ValuePtr<MetatypeValue> mt = dyn_cast<MetatypeValue>(type))
        return mt->alignment();

      const ValuePtr<>& result = type->context().get_functional(MetatypeAlignment(type, location));
      
      if (isa<UndefinedValue>(type))
        return undef(result->type(), location);
      
      return result;
    }

    /// \brief Get the type of blocks
    ValuePtr<> FunctionalBuilder::block_type(Context& context, const SourceLocation& location) {
      return context.get_functional(BlockType(context, location));
    }
    
    /// \brief Get the empty type
    ValuePtr<> FunctionalBuilder::empty_type(Context& context, const SourceLocation& location) {
      return context.get_functional(EmptyType(context, location));
    }
    
    /// \brief Get the unique value of the empty type
    ValuePtr<> FunctionalBuilder::empty_value(Context& context, const SourceLocation& location) {
      return context.get_functional(EmptyValue(context, location));
    }
    
    /// \brief Get the byte type
    ValuePtr<> FunctionalBuilder::byte_type(Context& context, const SourceLocation& location) {
      return context.get_functional(ByteType(context, location));
    }
    
    /// \brief Get the pointer-to-byte type
    ValuePtr<> FunctionalBuilder::byte_pointer_type(Context& context, const SourceLocation& location) {
      return pointer_type(byte_type(context, location), location);
    }
    
    /// \brief Get an undefined value of the specified type.
    ValuePtr<> FunctionalBuilder::undef(const ValuePtr<>& type, const SourceLocation& location) {
      return type->context().get_functional(UndefinedValue(type, location));
    }

    /// \brief Get an zero value of the specified type.
    ValuePtr<> FunctionalBuilder::zero(const ValuePtr<>& type, const SourceLocation& location) {
      if (ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(type)) {
        return int_value(int_ty, 0, location);
      } else if (ValuePtr<FloatType> float_ty = dyn_cast<FloatType>(type)) {
        PSI_FAIL("float zero not implemented");
      } else {
        return type->context().get_functional(ZeroValue(type, location));
      }
    }
    
    /**
     * \brief Get a the type of a pointer to a type.
     * 
     * \param target Type being pointed to.
     * \param upref Origin of this pointer.
     */
    ValuePtr<> FunctionalBuilder::pointer_type(const ValuePtr<>& target, const ValuePtr<>& upref, const SourceLocation& location) {
      return target->context().get_functional(PointerType(target, upref, location));
    }

    /**
     * \brief Get a the type of a pointer to a type.
     * 
     * \param target Type being pointed to.
     */
    ValuePtr<> FunctionalBuilder::pointer_type(const ValuePtr<>& target, const SourceLocation& location) {
      return pointer_type(target, ValuePtr<>(), location);
    }
    
    /**
     * \brief Get the type of upward references.
     */
    ValuePtr<> FunctionalBuilder::upref_type(Context& context, const SourceLocation& location) {
      return context.get_functional(UpwardReferenceType(context, location));
    }
    
    /**
     * \brief Get an upward reference.
     */
    ValuePtr<> FunctionalBuilder::upref(const ValuePtr<>& outer_type, const ValuePtr<>& index, const ValuePtr<>& next, const SourceLocation& location) {
      ValuePtr<> result = outer_type->context().get_functional(UpwardReference(outer_type, index, next, location));
      if (isa<UndefinedValue>(index))
        return undef(result->type(), location);
      return result;
    }
    
    /**
     * \brief Get an array type.
     * 
     * \param element_type The type of each element of the array.
     * 
     * \param length The array length.
     */
    ValuePtr<> FunctionalBuilder::array_type(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location) {
      return element_type->context().get_functional(ArrayType(element_type, length, location));
    }
    
    /// \copydoc FunctionalBuilder::array_type(Term*,Term*)
    ValuePtr<> FunctionalBuilder::array_type(const ValuePtr<>& element_type, unsigned length, const SourceLocation& location) {
      return array_type(element_type, size_value(element_type->context(), length, location), location);
    }
    
    /**
     * \brief Get a struct aggregate type.
     * 
     * \param context Present in case \c elements has zero length.
     * 
     * \param elements List of types of members of the struct.
     */
    ValuePtr<> FunctionalBuilder::struct_type(Context& context, const std::vector<ValuePtr<> >& elements, const SourceLocation& location) {
      return context.get_functional(StructType(context, elements, location));
    }
    
    /**
     * \brief Get a union aggregate type.
     * 
     * \param context Present in case \c elements has zero length.
     * 
     * \param elements List of types of members of the union.
     */
    ValuePtr<> FunctionalBuilder::union_type(Context& context, const std::vector<ValuePtr<> >& elements, const SourceLocation& location) {
      return context.get_functional(UnionType(context, elements, location));
    }

    /**
     * \brief Get a struct_eo operation.
     */
    ValuePtr<> FunctionalBuilder::struct_element_offset(const ValuePtr<>& struct_ty, unsigned index, const SourceLocation& location) {
      return struct_ty->context().get_functional(StructElementOffset(struct_ty, index, location));
    }
    
    /**
     * \brief Get an array_ep operation.
     * 
     * \param aggregate_ptr Pointer to an aggregate value.
     * \param member Which member of the aggregate to get a pointer to.
     */
    ValuePtr<> FunctionalBuilder::element_ptr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location) {
      ValuePtr<> result = aggregate_ptr->context().get_functional(ElementPtr(aggregate_ptr, index, location));
      if (isa<UndefinedValue>(aggregate_ptr) || isa<UndefinedValue>(index))
        return undef(result->type(), location);
      return result;
    }
    
    /**
     * \brief Get a pointer to an aggregate element.
     * 
     * \param aggregate Pointer to an aggregate.
     * 
     * \param index Index of element to get.
     */
    ValuePtr<> FunctionalBuilder::element_ptr(const ValuePtr<>& array, unsigned index, const SourceLocation& location) {
      return element_ptr(array, size_value(array->context(), index, location), location);
    }

    /**
     * \brief Get an outer_ptr operation.
     */
    ValuePtr<> FunctionalBuilder::outer_ptr(const ValuePtr<>& base, const SourceLocation& location) {
      ValuePtr<> result = base->context().get_functional(OuterPtr(base, location));
      if (isa<UndefinedValue>(result))
        return undef(result->type(), location);
      return result;
    }

    /**
     * Construct an array value.
     * 
     * \param element_type Type of array elements. Present in case
     * \c elements has zero length.
     * 
     * \param elements Values of array elements.
     */
    ValuePtr<> FunctionalBuilder::array_value(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location) {
      return element_type->context().get_functional(ArrayValue(element_type, elements, location));
    }
    
    /**
     * Construct a struct value.
     * 
     * \param context Context, in case \c elements has zero length.
     * 
     * \param elements Values of structure elements. The structure
     * type will be inferred from the types of these elements.
     */
    ValuePtr<> FunctionalBuilder::struct_value(Context& context, const std::vector<ValuePtr<> >& elements, const SourceLocation& location) {
      return context.get_functional(StructValue(context, elements, location));
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
    ValuePtr<> FunctionalBuilder::union_value(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location) {
      if (isa<UndefinedValue>(value))
        return undef(type, location);
      
      return type->context().get_functional(UnionValue(type, value, location));
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
    ValuePtr<> FunctionalBuilder::element_value(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location) {
      ValuePtr<> result = aggregate->context().get_functional(ElementValue(aggregate, index, location));

      if (isa<UndefinedValue>(aggregate) || isa<UndefinedValue>(index)) {
        return undef(result->type(), location);
      } else if (isa<ZeroValue>(aggregate)) {
        return zero(result->type(), location);
      } else if (ValuePtr<IntegerValue> index_val = dyn_cast<IntegerValue>(index)) {
        boost::optional<unsigned> index_ui = index_val->value().unsigned_value();
        if (!index_ui)
          throw TvmUserError("aggregate index out of range");
        
        if (ValuePtr<StructValue> struct_val = dyn_cast<StructValue>(aggregate)) {
          if (*index_ui < struct_val->n_members())
            return struct_val->member_value(*index_ui);
          else
            throw TvmUserError("struct element index out of range");
        } else if (ValuePtr<ArrayValue> array_val = dyn_cast<ArrayValue>(aggregate)) {
          if (*index_ui < array_val->length())
            return array_val->value(*index_ui);
          else
            throw TvmUserError("array element index out of range");
        }
      }
      
      return result;
    }
    
    /**
     * \brief Get the value of an aggregate element.
     * 
     * \param array Array being subscripted.
     * \param index Index into the array.
     */
    ValuePtr<> FunctionalBuilder::element_value(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location) {
      return element_value(aggregate, size_value(aggregate->context(), index, location), location);
    }
    
    namespace {
      ValuePtr<> pointer_target_type(const ValuePtr<>& ptr) {
        ValuePtr<PointerType> ptr_ty = dyn_unrecurse<PointerType>(ptr->type());
        if (!ptr_ty)
          throw TvmUserError("Parameter is not a pointer");
        return ptr_ty->target_type();
      }
    }
    
    /**
     * \brief Cast a pointer from one type to another.
     * 
     * \param ptr Original pointer.
     * 
     * \param result_type Type of pointed-to type of the new pointer (and
     * hence not necessarily a pointer type itself).
     */
    ValuePtr<> FunctionalBuilder::pointer_cast(const ValuePtr<>& ptr, const ValuePtr<>& result_type, const ValuePtr<>& upref, const SourceLocation& location) {
      ValuePtr<> my_ptr = ptr;
      // Try to get to the lowest pointer if multiple casts are involved
      while (true) {
        ValuePtr<PointerCast> cast_ptr = dyn_cast<PointerCast>(my_ptr);
        if (cast_ptr)
          my_ptr = cast_ptr->pointer();
        else
          break;
      }
      ValuePtr<PointerType> base_type = dyn_cast<PointerType>(my_ptr->type());
      if (!base_type)
        throw TvmUserError("Target of pointer_cast is not a pointer");

      if ((base_type->target_type() == result_type) && (base_type->upref() == upref)) {
        return my_ptr;
      } else {
        const ValuePtr<>& result = ptr->context().get_functional(PointerCast(my_ptr, result_type, upref, location));
        
        if (isa<UndefinedValue>(my_ptr))
          return undef(result->type(), location);
        
        return result;
      }
    }
    
    ValuePtr<> FunctionalBuilder::pointer_cast(const ValuePtr<>& ptr, const ValuePtr<>& result_type, const SourceLocation& location) {
      return pointer_cast(ptr, result_type, ValuePtr<>(), location);
    }
    
    /**
     * \brief Get a pointer which is at a specified offset from an existing pointer.
     * 
     * \param ptr Original pointer.
     * 
     * \param offset Offset from original pointer in units of the
     * pointed-to type.
     */
    ValuePtr<> FunctionalBuilder::pointer_offset(const ValuePtr<>& ptr, const ValuePtr<>& offset, const SourceLocation& location) {
      const ValuePtr<>& result = ptr->context().get_functional(PointerOffset(ptr, offset, location));
      
      if (isa<UndefinedValue>(ptr) || isa<UndefinedValue>(offset))
        return undef(result->type(), location);
      
      return result;
    }

    /// \copydoc FunctionalBuilder::pointer_offset(Term*,Term*)
    ValuePtr<> FunctionalBuilder::pointer_offset(const ValuePtr<>& ptr, unsigned offset, const SourceLocation& location) {
      const ValuePtr<>& result = ptr->context().get_functional(PointerOffset(ptr, size_value(ptr->context(), offset, location), location));
      
      if (!offset)
        return ptr;
      
      return result;
    }
    
    /// \brief Get the boolean type
    ValuePtr<> FunctionalBuilder::bool_type(Context& context, const SourceLocation& location) {
      return context.get_functional(BooleanType(context, location));
    }
    
    /// \brief Get a constant boolean value.
    ValuePtr<> FunctionalBuilder::bool_value(Context& context, bool value, const SourceLocation& location) {
      return context.get_functional(BooleanValue(context, value, location));
    }
    
    /// \brief Get an integer type
    ValuePtr<> FunctionalBuilder::int_type(Context& context, IntegerType::Width width, bool is_signed, const SourceLocation& location) {
      return context.get_functional(IntegerType(context, width, is_signed, location));
    }
    
    /// \brief Get the intptr type
    ValuePtr<> FunctionalBuilder::size_type(Context& context, const SourceLocation& location) {
      return int_type(context, IntegerType::iptr, false, location);
    }
    
    /**
     * \brief Get a constant integer value.
     *
     * This should only be used for small, known constants,
     * otherwise just construct a term representing whatever
     * arithmetic would be used to calculate the value.
     * 
     * \param value Value of the integer to get.
     */    
    ValuePtr<> FunctionalBuilder::int_value(Context& context, IntegerType::Width width, bool is_signed, int value, const SourceLocation& location) {
      return int_value(context, width, is_signed, BigInteger(IntegerType::value_bits(width), value), location);
    }

    /// \copydoc FunctionalBuilder::int_value(Term*,int)
    ValuePtr<> FunctionalBuilder::int_value(Context& context, IntegerType::Width width, bool is_signed, unsigned value, const SourceLocation& location) {
      return int_value(context, width, is_signed, BigInteger(IntegerType::value_bits(width), value), location);
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
    ValuePtr<> FunctionalBuilder::int_value(Context& context, IntegerType::Width width, bool is_signed, const std::string& value, bool negative, unsigned base, const SourceLocation& location) {
      BigInteger bv(IntegerType::value_bits(width));
      bv.parse(value, negative, base);
      return int_value(context, width, is_signed, bv, location);
    }

    ValuePtr<> FunctionalBuilder::int_value(Context& context, IntegerType::Width width, bool is_signed, const std::string& value, const SourceLocation& location) {
      return int_value(context, width, is_signed, value, false, 10, location);
    }

    ValuePtr<> FunctionalBuilder::int_value(Context& context, IntegerType::Width width, bool is_signed, const std::string& value, bool negative, const SourceLocation& location) {
      return int_value(context, width, is_signed, value, negative, 10, location);
    }

    ValuePtr<> FunctionalBuilder::int_value(const ValuePtr<IntegerType>& type, const std::string& value, const SourceLocation& location) {
      return int_value(type->context(), type->width(), type->is_signed(), value, false, 10, location);
    }

    ValuePtr<> FunctionalBuilder::int_value(const ValuePtr<IntegerType>& type, const std::string& value, bool negative, const SourceLocation& location) {
      return int_value(type->context(), type->width(), type->is_signed(), value, negative, 10, location);
    }

    /**
     * \brief Get a constant integer value.
     * 
     * \param value Value of the integer to create.
     */
    ValuePtr<> FunctionalBuilder::int_value(Context& context, IntegerType::Width width, bool is_signed, const BigInteger& value, const SourceLocation& location) {
      return context.get_functional(IntegerValue(context, width, is_signed, value, location));
    }
    
    /// \copydoc FunctionalBuilder::int_value(Context&,IntegerType::Width,bool,int)
    ValuePtr<> FunctionalBuilder::int_value(const ValuePtr<IntegerType>& type, int value, const SourceLocation& location) {
      return int_value(type->context(), type->width(), type->is_signed(), value, location);
    }
    
    /// \copydoc FunctionalBuilder::int_value(Context&,IntegerType::Width,bool,int)
    ValuePtr<> FunctionalBuilder::int_value(const ValuePtr<IntegerType>& type, unsigned value, const SourceLocation& location) {
      return int_value(type->context(), type->width(), type->is_signed(), value, location);
    }
    
    /// \copydoc FunctionalBuilder::int_value(Context&,IntegerType::Width,bool,const std::string&,bool,unsigned)
    ValuePtr<> FunctionalBuilder::int_value(const ValuePtr<IntegerType>& type, const std::string& value, bool negative, unsigned base, const SourceLocation& location) {
      return int_value(type->context(), type->width(), type->is_signed(), value, negative, base, location);
    }
    
    /// \copydoc FunctionalBuilder::int_value(Context&,IntegerType::Width,bool,const BigInteger&)
    ValuePtr<> FunctionalBuilder::int_value(const ValuePtr<IntegerType>& type, const BigInteger& value, const SourceLocation& location) {
      return int_value(type->context(), type->width(), type->is_signed(), value, location);
    }

    /**
     * Get a uintptr constant containing the given value.
     * 
     * This is just a utility function that uses int_value and
     * size_type, but saves some typing.
     * 
     * \param context Context to create the constant in.
     * 
     * \param value Value to initialize the constant with.
     */
    ValuePtr<> FunctionalBuilder::size_value(Context& context, unsigned value, const SourceLocation& location) {
      return int_value(context, IntegerType::iptr, false, value, location);
    }
    
    namespace {      
      /**
       * Combine constants on commutative operations.
       * 
       * This assumes that a commutative operation involving a constant will
       * have the constant on the far left, i.e. it will be <tt>OP CONST VALUE</tt>,
       * where \c VALUE contains no further constants.
       * 
       * \tparam CommutativeOp Commutative operation term type.
       * 
       * \tparam ConstType Term type to look for which is the constant
       * for this operation.
       * 
       * \param const_combiner A callback which works out the result of the
       * commutative operation on a pair of constants.
       */
      template<typename CommutativeOp, typename ConstType, typename ConstCombiner>
      ValuePtr<> commutative_simplify(ValuePtr<> lhs, ValuePtr<> rhs, const ConstCombiner& const_combiner, const SourceLocation& location) {
        Context& context = lhs->context();

        ValuePtr<ConstType> const_lhs = dyn_cast<ConstType>(lhs), const_rhs = dyn_cast<ConstType>(rhs);
        if (const_lhs && const_rhs) {
          return const_combiner(const_lhs, const_rhs, location);
        } else if (const_lhs || const_rhs) {
          if (!const_lhs) {
            std::swap(const_lhs, const_rhs);
            std::swap(lhs, rhs);
          }
          PSI_ASSERT(const_lhs && !const_rhs);
          
          if (ValuePtr<CommutativeOp> com_op_rhs = dyn_cast<CommutativeOp>(rhs))
            if (ValuePtr<ConstType> const_rhs_lhs = dyn_cast<ConstType>(com_op_rhs->lhs()))
              return context.get_functional(CommutativeOp(const_combiner(const_lhs, const_rhs_lhs, location), com_op_rhs->rhs(), location));

          return context.get_functional(CommutativeOp(const_lhs, rhs, location));
        } else {
          PSI_ASSERT(!const_lhs && !const_rhs);
          ValuePtr<CommutativeOp> com_op_lhs = dyn_cast<CommutativeOp>(lhs), com_op_rhs = dyn_cast<CommutativeOp>(rhs);
          ValuePtr<ConstType> const_lhs_lhs = com_op_lhs ? dyn_cast<ConstType>(com_op_lhs->lhs()) : ValuePtr<ConstType>();
          ValuePtr<ConstType> const_rhs_lhs = com_op_rhs ? dyn_cast<ConstType>(com_op_rhs->lhs()) : ValuePtr<ConstType>();
          
          if (const_lhs_lhs && const_rhs_lhs) {
            return context.get_functional(CommutativeOp(const_combiner(const_lhs_lhs, const_rhs_lhs, location),
                                                        context.get_functional(CommutativeOp(com_op_lhs->rhs(), com_op_rhs->rhs(), location)), location));
          } else if (const_lhs_lhs) {
            return context.get_functional(CommutativeOp(const_lhs_lhs, context.get_functional(CommutativeOp(com_op_lhs->rhs(), rhs, location)), location));
          } else if (const_rhs_lhs) {
            return context.get_functional(CommutativeOp(const_rhs_lhs, context.get_functional(CommutativeOp(lhs, com_op_rhs->rhs(), location)), location));
          } else {
            return context.get_functional(CommutativeOp(lhs, rhs, location));
          }
        }
      }
      
      class IntConstCombiner {
      public:
        typedef void (BigInteger::*CallbackType) (const BigInteger&, const BigInteger&);
        
        explicit IntConstCombiner(CallbackType callback) : m_callback(callback) {
        }
        
        ValuePtr<> operator () (const ValuePtr<IntegerValue>& lhs, const ValuePtr<IntegerValue>& rhs, const SourceLocation& location) const {
          BigInteger value;
          (value.*m_callback)(lhs->value(), rhs->value());
          return FunctionalBuilder::int_value(lhs->type(), value, location);
        }

      private:
        CallbackType m_callback;
      };

      ValuePtr<> int_binary_undef(const char *op, const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
        if (lhs->type() != rhs->type())
          throw TvmUserError(std::string("type mismatch on parameter to") + op);
        else if (!isa<IntegerType>(lhs->type()))
          throw TvmUserError(std::string("parameters to ") + op + " are not integers");
        return FunctionalBuilder::undef(lhs->type(), location);
      }
      
      ValuePtr<> int_unary_undef(const char *op, const ValuePtr<>& parameter, const SourceLocation& location) {
        if (!isa<IntegerType>(parameter->type()))
          throw TvmUserError(std::string("parameters to ") + op +  " are not integers");
        return FunctionalBuilder::undef(parameter->type(), location);
      }
    }

    /// \brief Get an integer add operation.
    ValuePtr<> FunctionalBuilder::add(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      if (isa<UndefinedValue>(lhs) || isa<UndefinedValue>(rhs)) {
        return int_binary_undef(IntegerAdd::operation, lhs, rhs, location);
      } else if (isa<IntegerNegative>(lhs) && isa<IntegerNegative>(rhs)) {
        return neg(add(value_cast<IntegerNegative>(lhs)->parameter(), value_cast<IntegerNegative>(rhs)->parameter(), location), location);
      } else {
        return commutative_simplify<IntegerAdd, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::add), location);
      }
    }
    
    /// \brief Get an integer subtract operation.
    ValuePtr<> FunctionalBuilder::sub(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return add(lhs, neg(rhs, location), location);
    }
    
    /// \brief Get an integer multiply operation.
    ValuePtr<> FunctionalBuilder::mul(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return lhs->context().get_functional(IntegerMultiply(lhs, rhs, location));
    }
    
    /// \brief Get an integer division operation.
    ValuePtr<> FunctionalBuilder::div(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return lhs->context().get_functional(IntegerDivide(lhs, rhs, location));
    }
    
    /// \brief Get an integer negation operation
    ValuePtr<> FunctionalBuilder::neg(const ValuePtr<>& parameter, const SourceLocation& location) {
      if (isa<UndefinedValue>(parameter)) {
        return int_unary_undef(IntegerNegative::operation, parameter, location);
      } else if (ValuePtr<IntegerNegative> neg_op = dyn_cast<IntegerNegative>(parameter)) {
        return neg_op->parameter();
      } else if (ValuePtr<IntegerValue> int_val = dyn_cast<IntegerValue>(parameter)) {
        BigInteger value;
        value.negative(int_val->value());
        return int_value(int_val->type(), value, location);
      } else {
        return parameter->context().get_functional(IntegerNegative(parameter, location));
      }
    }      
    
    /// \brief Get a bitwise and operation
    ValuePtr<> FunctionalBuilder::bit_and(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      if (isa<UndefinedValue>(lhs) && isa<UndefinedValue>(rhs)) {
        return int_binary_undef(BitAnd::operation, lhs, rhs, location);
      } else {
        return commutative_simplify<BitAnd, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::bit_and), location);
      }
    }
    
    /// \brief Get a bitwise or operation
    ValuePtr<> FunctionalBuilder::bit_or(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      if (isa<UndefinedValue>(lhs) || isa<UndefinedValue>(rhs)) {
        return int_binary_undef(BitOr::operation, lhs, rhs, location);
      } else {
        return commutative_simplify<BitOr, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::bit_or), location);
      }
    }
    
    /// \brief Get a bitwise exclusive or operation
    ValuePtr<> FunctionalBuilder::bit_xor(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      if (isa<UndefinedValue>(lhs) || isa<UndefinedValue>(rhs)) {
        return int_binary_undef(BitXor::operation, lhs, rhs, location);
      } else {
        return commutative_simplify<BitXor, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::bit_xor), location);
      }
    }
    
    /// \brief Get a bitwise inverse operation
    ValuePtr<> FunctionalBuilder::bit_not(const ValuePtr<>& parameter, const SourceLocation& location) {
      if (isa<UndefinedValue>(parameter)) {
        return int_unary_undef(BitNot::operation, parameter, location);
      } else if (ValuePtr<BitNot> not_op = dyn_cast<BitNot>(parameter)) {
        return not_op->parameter();
      } else if (ValuePtr<IntegerValue> int_val = dyn_cast<IntegerValue>(parameter)) {
        BigInteger value;
        value.bit_not(int_val->value());
        return int_value(int_val->type(), value, location);
      } else {
        return parameter->context().get_functional(BitNot(parameter, location));
      }
    }
    
    namespace {
      class CompareShortcut {
      public:
        enum Category {
          undef,
          true_,
          false_
        };
        
        CompareShortcut(Category c) : m_c(c) {}
        
        ValuePtr<> operator() (Context& context, const SourceLocation& location) const {
          switch (m_c) {
          case undef: return FunctionalBuilder::undef(FunctionalBuilder::bool_type(context, location), location);
          case true_: return FunctionalBuilder::bool_value(context, true, location);
          case false_: return FunctionalBuilder::bool_value(context, false, location);
          default: PSI_FAIL("unexpected value");
          }
        }
            
        
      private:
        Category m_c;
      };
      
      /**
       * \param lhs_undef_min If lhs is undef, what value does the expression take when
       * rhs is min.
       * 
       * \param lhs_undef_max If lhs is undef, what value does the expression take when
       * rhs is max.
       * 
       * \param rhs_undef_min If rhs is undef, what value does the expression take when
       * lhs is min.
       * 
       * \param rhs_undef_max If rhs is undef, what value does the expression take when
       * lhs is max.
       */
      template<typename Op, typename Comparator>
      ValuePtr<> cmp_op(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const Comparator& cmp,
                        CompareShortcut lhs_undef_min, CompareShortcut lhs_undef_max,
                        CompareShortcut rhs_undef_min, CompareShortcut rhs_undef_max,
                        const SourceLocation& location) {
        if (lhs->type() != rhs->type())
          throw TvmUserError(std::string("type mismatch on parameters to ") + Op::operation + " operation");
        ValuePtr<IntegerType> int_ty = dyn_cast<IntegerType>(lhs->type());
        if (!int_ty)
          throw TvmUserError(std::string("parameters to ") + Op::operation + " are not integers");
        bool int_ty_signed = int_ty->is_signed();
        
        bool lhs_undef = isa<UndefinedValue>(lhs), rhs_undef = isa<UndefinedValue>(rhs);
        ValuePtr<IntegerValue> lhs_val = dyn_cast<IntegerValue>(lhs), rhs_val = dyn_cast<IntegerValue>(rhs);
        
        if (lhs_undef && rhs_undef) {
          return FunctionalBuilder::undef(FunctionalBuilder::bool_type(int_ty->context(), location), location);
        } else if (lhs_undef) {
          if (rhs_val) {
            if (rhs_val->value().is_max(int_ty_signed))
              return lhs_undef_max(int_ty->context(), location);
            else if (rhs_val->value().is_min(int_ty_signed))
              return lhs_undef_min(int_ty->context(), location);
            else
              return FunctionalBuilder::undef(FunctionalBuilder::bool_type(int_ty->context(), location), location);
          }
        } else if (rhs_undef) {
          if (lhs_val) {
            if (lhs_val->value().is_max(int_ty_signed))
              return rhs_undef_max(int_ty->context(), location);
            else if (lhs_val->value().is_min(int_ty_signed))
              return rhs_undef_min(int_ty->context(), location);
            else
              return FunctionalBuilder::undef(FunctionalBuilder::bool_type(int_ty->context(), location), location);
          }
        } else if (lhs_val && rhs_val) {
          int cmp_val = int_ty->is_signed() ? lhs_val->value().cmp_signed(rhs_val->value()) : lhs_val->value().cmp_unsigned(rhs_val->value());
          return FunctionalBuilder::bool_value(int_ty->context(), cmp(cmp_val, 0), location);
        }
        
        return lhs->context().get_functional(Op(lhs, rhs, location));
      }
    }

    /// \brief Get an integer == comparison operation
    ValuePtr<> FunctionalBuilder::cmp_eq(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return cmp_op<IntegerCompareEq>(lhs, rhs, std::equal_to<int>(), CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef, location);
    }
    
    /// \brief Get an integer != comparison operation
    ValuePtr<> FunctionalBuilder::cmp_ne(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return cmp_op<IntegerCompareNe>(lhs, rhs, std::not_equal_to<int>(), CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef, location);
    }
    
    /// \brief Get an integer \> comparison operation
    ValuePtr<> FunctionalBuilder::cmp_gt(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return cmp_op<IntegerCompareGt>(lhs, rhs, std::greater<int>(), CompareShortcut::undef, CompareShortcut::false_, CompareShortcut::false_, CompareShortcut::undef, location);
    }
    
    /// \brief Get an integer \> comparison operation
    ValuePtr<> FunctionalBuilder::cmp_ge(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return cmp_op<IntegerCompareGe>(lhs, rhs, std::greater_equal<int>(), CompareShortcut::true_, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::true_, location);
    }
    
    /// \brief Get an integer \< comparison operation
    ValuePtr<> FunctionalBuilder::cmp_lt(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return cmp_op<IntegerCompareLt>(lhs, rhs, std::less<int>(), CompareShortcut::false_, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::false_, location);
    }
    
    /// \brief Get an integer \<= comparison operation
    ValuePtr<> FunctionalBuilder::cmp_le(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      return cmp_op<IntegerCompareLe>(lhs, rhs, std::less_equal<int>(), CompareShortcut::undef, CompareShortcut::true_, CompareShortcut::true_, CompareShortcut::undef, location);
    }

    /// \brief Get the maximum of two integers
    ValuePtr<> FunctionalBuilder::max(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      ValuePtr<> cond = cmp_ge(lhs, rhs, location);
      return select(cond, lhs, rhs, location);
    }
    
    /// \brief Get the minimum of two integers
    ValuePtr<> FunctionalBuilder::min(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location) {
      ValuePtr<> cond = cmp_le(lhs, rhs, location);
      return select(cond, lhs, rhs, location);
    }

    /**
     * \brief Align an offset to a specified alignment, which must be a
     * power of two.
     *
     * The formula used is: <tt>(offset + align - 1) & ~(align - 1)</tt>
     */
    ValuePtr<> FunctionalBuilder::align_to(const ValuePtr<>& offset, const ValuePtr<>& align, const SourceLocation& location) {
      const ValuePtr<>& one = size_value(offset->context(), 1, location);
      const ValuePtr<>& align_minus_one = sub(align, one, location);
      const ValuePtr<>& offset_plus_align_minus_one = add(offset, align_minus_one, location);
      const ValuePtr<>& not_align_minus_one = bit_not(align_minus_one, location);
      return bit_and(offset_plus_align_minus_one, not_align_minus_one, location);
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
    ValuePtr<> FunctionalBuilder::select(const ValuePtr<>& condition, const ValuePtr<>& if_true, const ValuePtr<>& if_false, const SourceLocation& location) {
      ValuePtr<> result = condition->context().get_functional(Select(condition, if_true, if_false, location));
      if (if_true == if_false)
        return if_true;
      if (ValuePtr<BooleanValue> bool_val = dyn_cast<BooleanValue>(condition))
        return bool_val->value() ? if_true : if_false;
      
      /* 
       * Can't set to undef if any of the incoming values is undefined because it is
       * reasonable to expect that the select operation returns one of the values
       * regardless of the condition.
       */
      if (isa<UndefinedValue>(condition) && (isa<UndefinedValue>(if_true) || isa<UndefinedValue>(if_false)))
        return undef(result->type(), location);
      return result;
    }

    /**
     * Specialize a function by binding values to its phantom parameters.
     */
    ValuePtr<> FunctionalBuilder::specialize(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {      
      if (parameters.size() == 0) {
        if (!isa<PointerType>(function) || !isa<FunctionType>(value_cast<PointerType>(function)->target_type()))
          throw TvmUserError("specialize target is not a function pointer");

        return function;
      }
      
      return function->context().get_functional(FunctionSpecialize(function, parameters, location));
    }
    
    /**
     * Get a floating point type.
     */
    ValuePtr<> FunctionalBuilder::float_type(Context& context, FloatType::Width width, const SourceLocation& location) {
      return context.get_functional(FloatType(context, width, location));
    }

    /**
     * \brief Specialize a recursive type.
     */
    ValuePtr<> FunctionalBuilder::apply(const ValuePtr<>& recursive, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location) {
      return recursive->context().get_functional(ApplyValue(recursive, parameters, location));
    }
    
    ValuePtr<> FunctionalBuilder::unwrap(const ValuePtr<>& value, const SourceLocation& location) {
      return value->context().get_functional(Unwrap(value, location));
    }
    
    /**
     * \brief Create an unwrap parameter.
     */
    ValuePtr<> FunctionalBuilder::unwrap_param(const ValuePtr<>& value, unsigned index, const SourceLocation& location) {
      return value->context().get_functional(UnwrapParameter(value, index, location));
    }
  }
}