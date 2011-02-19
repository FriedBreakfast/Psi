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
      }
      
      Term *result = MetatypeSize::get(type);
      
      if (isa<UndefinedValue>(type))
        return undef(result->type());
      
      return result;
    }
    
    /// \brief Get the alignment of a type.
    Term* FunctionalBuilder::type_alignment(Term *type) {
      if (MetatypeValue::Ptr mt = dyn_cast<MetatypeValue>(type))
        return mt->alignment();

      Term *result = MetatypeAlignment::get(type);
      
      if (isa<UndefinedValue>(type))
        return undef(result->type());
      
      return result;
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
    
    /// \brief Get the byte type
    Term* FunctionalBuilder::byte_type(Context& context) {
      return ByteType::get(context);
    }
    
    /// \brief Get the pointer-to-byte type
    Term* FunctionalBuilder::byte_pointer_type(Context& context) {
      return PointerType::get(ByteType::get(context));
    }
    
    /// \brief Get an undefined value of the specified type.
    Term* FunctionalBuilder::undef(Term *type) {
      return UndefinedValue::get(type);
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
    
    /// \copydoc FunctionalBuilder::array_type(Term*,Term*)
    Term* FunctionalBuilder::array_type(Term *element_type, unsigned length) {
      return array_type(element_type, size_value(element_type->context(), length));
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
      Term *result = UnionValue::get(cast<UnionType>(type), value);
      
      if (isa<UndefinedValue>(value))
        return undef(type);
      
      return result;
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
          boost::optional<unsigned> index_ui = index_val->value().unsigned_value();
          if (index_ui && (*index_ui < array_val->length()))
            return array_val->value(*index_ui);
          else
            throw TvmUserError("array index out of range");
        }
      } else if (isa<UndefinedValue>(array) || isa<UndefinedValue>(index)) {
        return undef(result->type());
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
      } else if (isa<UndefinedValue>(aggregate)) {
        return undef(result->type());
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
      } else if (isa<UndefinedValue>(aggregate)) {
        return undef(result->type());
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
      Term *result = ArrayElementPtr::get(array, index);
      
      if (isa<UndefinedValue>(array) || isa<UndefinedValue>(index))
        return undef(result->type());
      
      return result;
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
      Term *result = StructElementPtr::get(aggregate, index);
      
      if (isa<UndefinedValue>(aggregate))
        return undef(result->type());
      
      return result;
    }
    
    /**
     * \brief Get a pointer to a union member.
     * 
     * \param aggregate Pointer to a union.
     * 
     * \param type Member type to get a pointer to.
     */
    Term* FunctionalBuilder::union_element_ptr(Term *aggregate, Term *type) {
      Term *result = UnionElementPtr::get(aggregate, type);
      
      if (isa<UndefinedValue>(aggregate))
        return undef(result->type());
      
      return result;
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
     * \brief Get the offset of a struct element.
     * 
     * \param type Struct type being examined.
     * 
     * \param index Index of member to get the offset of.
     */
    Term* FunctionalBuilder::struct_element_offset(Term *type, unsigned index) {
      return StructElementOffset::get(type, index);
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
      PointerType::Ptr base_type = cast<PointerType>(ptr->type());
      if (base_type->target_type() == result_type) {
        return ptr;
      } else {
        Term *result = PointerCast::get(ptr, result_type);
        
        if (isa<UndefinedValue>(ptr))
          return undef(result->type());
        
        return result;
      }
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
      Term *result = PointerOffset::get(ptr, offset);
      
      if (isa<UndefinedValue>(ptr) || isa<UndefinedValue>(offset))
        return undef(result->type());
      
      return result;
    }

    /// \copydoc FunctionalBuilder::pointer_offset(Term*,Term*)
    Term* FunctionalBuilder::pointer_offset(Term *ptr, unsigned offset) {
      Term *result = PointerOffset::get(ptr, size_value(ptr->context(), offset));
      
      if (!offset)
        return ptr;
      
      return result;
    }
    
    /// \brief Get the boolean type
    Term* FunctionalBuilder::bool_type(Context& context) {
      return BooleanType::get(context);
    }
    
    /// \brief Get a constant boolean value.
    Term* FunctionalBuilder::bool_value(Context& context, bool value) {
      return BooleanValue::get(context, value);
    }
    
    /// \brief Get an integer type
    Term* FunctionalBuilder::int_type(Context& context, IntegerType::Width width, bool is_signed) {
      return IntegerType::get(context, width, is_signed);
    }
    
    /// \brief Get the intptr type
    Term* FunctionalBuilder::size_type(Context& context) {
      return int_type(context, IntegerType::iptr, false);
    }
    
    namespace {
      /**
       * \brief Get the number of bits used to store an integer value of the given type.
       * 
       * \see IntegerValue::value_bits
       */
      unsigned int_value_bits(Term *type) {
        if (IntegerType::Ptr int_ty = dyn_cast<IntegerType>(type)) {
          return IntegerValue::value_bits(int_ty->width());
        } else {
          throw TvmUserError("type of integer value is not an integer type");
        }
      }
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
      return IntegerValue::get(type, BigInteger(int_value_bits(type), value));
    }

    /// \copydoc FunctionalBuilder::int_value(Term*,int)
    Term* FunctionalBuilder::int_value(Term *type, unsigned value) {
      return IntegerValue::get(type, BigInteger(int_value_bits(type), value));
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
      BigInteger bv(int_value_bits(type));
      bv.parse(value, negative, base);
      return IntegerValue::get(type, bv);
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
    Term* FunctionalBuilder::size_value(Context& context, unsigned value) {
      return int_value(size_type(context), value);
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
      Term* commutative_simplify(Term *lhs, Term *rhs, const ConstCombiner& const_combiner) {
        typename ConstType::Ptr const_lhs = dyn_cast<ConstType>(lhs), const_rhs = dyn_cast<ConstType>(rhs);
        if (const_lhs && const_rhs) {
          return const_combiner(const_lhs, const_rhs);
        } else if (const_lhs || const_rhs) {
          if (!const_lhs) {
            std::swap(const_lhs, const_rhs);
            std::swap(lhs, rhs);
          }
          PSI_ASSERT(const_lhs && !const_rhs);
          
          if (typename CommutativeOp::Ptr com_op_rhs = dyn_cast<CommutativeOp>(rhs))
            if (typename ConstType::Ptr const_rhs_lhs = dyn_cast<ConstType>(com_op_rhs->lhs()))
              return CommutativeOp::get(const_combiner(const_lhs, const_rhs_lhs), com_op_rhs->rhs());

          return CommutativeOp::get(const_lhs, rhs);
        } else {
          PSI_ASSERT(!const_lhs && !const_rhs);
          typename CommutativeOp::Ptr com_op_lhs = dyn_cast<CommutativeOp>(lhs), com_op_rhs = dyn_cast<CommutativeOp>(rhs);
          typename ConstType::Ptr const_lhs_lhs = com_op_lhs ? dyn_cast<ConstType>(com_op_lhs->lhs()) : typename ConstType::Ptr();
          typename ConstType::Ptr const_rhs_lhs = com_op_rhs ? dyn_cast<ConstType>(com_op_rhs->lhs()) : typename ConstType::Ptr();
          
          if (const_lhs_lhs && const_rhs_lhs) {
            return CommutativeOp::get(const_combiner(const_lhs_lhs, const_rhs_lhs),
                                      CommutativeOp::get(com_op_lhs->rhs(), com_op_rhs->rhs()));
          } else if (const_lhs_lhs) {
            return CommutativeOp::get(const_lhs_lhs, CommutativeOp::get(com_op_lhs->rhs(), rhs));
          } else if (const_rhs_lhs) {
            return CommutativeOp::get(const_rhs_lhs, CommutativeOp::get(lhs, com_op_rhs->rhs()));
          } else {
            return CommutativeOp::get(lhs, rhs);
          }
        }
      }
      
      class IntConstCombiner {
      public:
        typedef void (BigInteger::*CallbackType) (const BigInteger&, const BigInteger&);
        
        explicit IntConstCombiner(CallbackType callback) : m_callback(callback) {
        }
        
        Term* operator () (IntegerValue::Ptr lhs, IntegerValue::Ptr rhs) const {
          BigInteger value;
          (value.*m_callback)(lhs->value(), rhs->value());
          return IntegerValue::get(lhs->type(), value);
        }

      private:
        CallbackType m_callback;
      };

      Term* int_binary_undef(const char *op, Term *lhs, Term *rhs) {
        if (lhs->type() != rhs->type())
          throw TvmUserError(std::string("type mismatch on parameter to") + op);
        else if (!isa<IntegerType>(lhs->type()))
          throw TvmUserError(std::string("parameters to ") + op + " are not integers");
        return FunctionalBuilder::undef(lhs->type());
      }
      
      Term* int_unary_undef(const char *op, Term *parameter) {
        if (!isa<IntegerType>(parameter->type()))
          throw TvmUserError(std::string("parameters to ") + op +  " are not integers");
        return FunctionalBuilder::undef(parameter->type());
      }
    }

    /// \brief Get an integer add operation.
    Term *FunctionalBuilder::add(Term *lhs, Term *rhs) {
      if (isa<UndefinedValue>(lhs) || isa<UndefinedValue>(rhs)) {
        return int_binary_undef(IntegerAdd::operation, lhs, rhs);
      } else if (isa<IntegerNegative>(lhs) && isa<IntegerNegative>(rhs)) {
        return neg(add(cast<IntegerNegative>(lhs)->parameter(), cast<IntegerNegative>(rhs)->parameter()));
      } else {
        return commutative_simplify<IntegerAdd, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::add));
      }
    }
    
    /// \brief Get an integer subtract operation.
    Term *FunctionalBuilder::sub(Term *lhs, Term *rhs) {
      return add(lhs, neg(rhs));
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
      if (isa<UndefinedValue>(parameter)) {
        return int_unary_undef(IntegerNegative::operation, parameter);
      } else if (IntegerNegative::Ptr neg_op = dyn_cast<IntegerNegative>(parameter)) {
        return neg_op->parameter();
      } else if (IntegerValue::Ptr int_val = dyn_cast<IntegerValue>(parameter)) {
        BigInteger value;
        value.negative(int_val->value());
        return IntegerValue::get(int_val->type(), value);
      } else {
        return IntegerNegative::get(parameter);
      }
    }      
    
    /// \brief Get a bitwise and operation
    Term *FunctionalBuilder::bit_and(Term *lhs, Term *rhs) {
      if (isa<UndefinedValue>(lhs) && isa<UndefinedValue>(rhs)) {
        return int_binary_undef(BitAnd::operation, lhs, rhs);
      } else {
        return commutative_simplify<BitAnd, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::bit_and));
      }
    }
    
    /// \brief Get a bitwise or operation
    Term *FunctionalBuilder::bit_or(Term *lhs, Term *rhs) {
      if (isa<UndefinedValue>(lhs) || isa<UndefinedValue>(rhs)) {
        return int_binary_undef(BitOr::operation, lhs, rhs);
      } else {
        return commutative_simplify<BitOr, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::bit_or));
      }
    }
    
    /// \brief Get a bitwise exclusive or operation
    Term *FunctionalBuilder::bit_xor(Term *lhs, Term *rhs) {
      if (isa<UndefinedValue>(lhs) || isa<UndefinedValue>(rhs)) {
        return int_binary_undef(BitXor::operation, lhs, rhs);
      } else {
        return commutative_simplify<BitXor, IntegerValue>(lhs, rhs, IntConstCombiner(&BigInteger::bit_xor));
      }
    }
    
    /// \brief Get a bitwise inverse operation
    Term *FunctionalBuilder::bit_not(Term *parameter) {
      if (isa<UndefinedValue>(parameter)) {
        return int_unary_undef(BitNot::operation, parameter);
      } else if (BitNot::Ptr not_op = dyn_cast<BitNot>(parameter)) {
        return not_op->parameter();
      } else if (IntegerValue::Ptr int_val = dyn_cast<IntegerValue>(parameter)) {
        BigInteger value;
        value.bit_not(int_val->value());
        return IntegerValue::get(int_val->type(), value);
      } else {
        return BitNot::get(parameter);
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
        
        Term* operator() (Context& context) const {
          switch (m_c) {
          case undef: return FunctionalBuilder::undef(FunctionalBuilder::bool_type(context));
          case true_: return FunctionalBuilder::bool_value(context, true);
          case false_: return FunctionalBuilder::bool_value(context, false);
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
      Term *cmp_op(Term *lhs, Term *rhs, const Comparator& cmp,
                   CompareShortcut lhs_undef_min, CompareShortcut lhs_undef_max,
                   CompareShortcut rhs_undef_min, CompareShortcut rhs_undef_max) {
        if (lhs->type() != rhs->type())
          throw TvmUserError(std::string("type mismatch on parameters to ") + Op::operation + " operation");
        IntegerType::Ptr int_ty = dyn_cast<IntegerType>(lhs->type());
        if (!int_ty)
          throw TvmUserError(std::string("parameters to ") + Op::operation + " are not integers");
        bool int_ty_signed = int_ty->is_signed();
        
        bool lhs_undef = isa<UndefinedValue>(lhs), rhs_undef = isa<UndefinedValue>(rhs);
        IntegerValue::Ptr lhs_val = dyn_cast<IntegerValue>(lhs), rhs_val = dyn_cast<IntegerValue>(rhs);
        
        if (lhs_undef && rhs_undef) {
          return FunctionalBuilder::undef(FunctionalBuilder::bool_type(int_ty->context()));
        } else if (lhs_undef) {
          if (rhs_val) {
            if (rhs_val->value().is_max(int_ty_signed))
              return lhs_undef_max(int_ty->context());
            else if (rhs_val->value().is_min(int_ty_signed))
              return lhs_undef_min(int_ty->context());
            else
              return FunctionalBuilder::undef(FunctionalBuilder::bool_type(int_ty->context()));
          }
        } else if (rhs_undef) {
          if (lhs_val) {
            if (lhs_val->value().is_max(int_ty_signed))
              return rhs_undef_max(int_ty->context());
            else if (lhs_val->value().is_min(int_ty_signed))
              return rhs_undef_min(int_ty->context());
            else
              return FunctionalBuilder::undef(FunctionalBuilder::bool_type(int_ty->context()));
          }
        } else if (lhs_val && rhs_val) {
          int cmp_val = int_ty->is_signed() ? lhs_val->value().cmp_signed(rhs_val->value()) : lhs_val->value().cmp_unsigned(rhs_val->value());
          return FunctionalBuilder::bool_value(int_ty->context(), cmp(cmp_val, 0));
        }
        
        return Op::get(lhs, rhs);
      }
    }

    /// \brief Get an integer == comparison operation
    Term *FunctionalBuilder::cmp_eq(Term *lhs, Term *rhs) {
      return cmp_op<IntegerCompareEq>(lhs, rhs, std::equal_to<int>(), CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef);
    }
    
    /// \brief Get an integer != comparison operation
    Term *FunctionalBuilder::cmp_ne(Term *lhs, Term *rhs) {
      return cmp_op<IntegerCompareNe>(lhs, rhs, std::not_equal_to<int>(), CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::undef);
    }
    
    /// \brief Get an integer \> comparison operation
    Term *FunctionalBuilder::cmp_gt(Term *lhs, Term *rhs) {
      return cmp_op<IntegerCompareGt>(lhs, rhs, std::greater<int>(), CompareShortcut::undef, CompareShortcut::false_, CompareShortcut::false_, CompareShortcut::undef);
    }
    
    /// \brief Get an integer \> comparison operation
    Term *FunctionalBuilder::cmp_ge(Term *lhs, Term *rhs) {
      return cmp_op<IntegerCompareGe>(lhs, rhs, std::greater_equal<int>(), CompareShortcut::true_, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::true_);
    }
    
    /// \brief Get an integer \< comparison operation
    Term *FunctionalBuilder::cmp_lt(Term *lhs, Term *rhs) {
      return cmp_op<IntegerCompareLt>(lhs, rhs, std::less<int>(), CompareShortcut::false_, CompareShortcut::undef, CompareShortcut::undef, CompareShortcut::false_);
    }
    
    /// \brief Get an integer \<= comparison operation
    Term *FunctionalBuilder::cmp_le(Term *lhs, Term *rhs) {
      return cmp_op<IntegerCompareLe>(lhs, rhs, std::less_equal<int>(), CompareShortcut::undef, CompareShortcut::true_, CompareShortcut::true_, CompareShortcut::undef);
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
      Term *one = size_value(offset->context(), 1);
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
      Term *result = SelectValue::get(condition, if_true, if_false);
      if (if_true == if_false)
        return if_true;
      if (BooleanValue::Ptr bool_val = dyn_cast<BooleanValue>(condition))
        return bool_val->value() ? if_true : if_false;
      
      /* 
       * Can't set to undef if any of the incoming values is undefined because it is
       * reasonable to expect that the select operation returns one of the values
       * regardless of the condition.
       */
      if (isa<UndefinedValue>(condition) && (isa<UndefinedValue>(if_true) || isa<UndefinedValue>(if_false)))
        return undef(result->type());
      return result;
    }

    /**
     * Specialize a function by binding values to its phantom parameters.
     */
    Term* FunctionalBuilder::specialize(Term *function, ArrayPtr<Term*const> parameters) {      
      if (parameters.size() == 0) {
        if (!isa<PointerType>(function) || !isa<FunctionTypeTerm>(cast<PointerType>(function)->target_type()))
          throw TvmUserError("specialize target is not a function pointer");

        return function;
      }
      
      return FunctionSpecialize::get(function, parameters);
    }
    
    /**
     * Get a floating point type.
     */
    Term* FunctionalBuilder::float_type(Context& context, FloatType::Width width) {
      return FloatType::get(context, width);
    }
  }
}