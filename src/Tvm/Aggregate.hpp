#ifndef PSI_TVM_AGGREGATE
#define PSI_TVM_AGGREGATE

#include "Functional.hpp"
#include "Function.hpp"

/**
 * \file
 *
 * Definitions for aggregate types, plus a few miscellaneous core
 * types and operations that don't really fit anywhere else (Metatype,
 * EmptyType, EmptyValue, BlockType, FunctionSpecialize).
 */

namespace Psi {
  namespace Tvm {
    /**
     * \brief The empty type.
     * 
     * This is not equivalent to a struct, union or array
     * with no elements.
     */
    class PSI_TVM_EXPORT_DEBUG EmptyType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(EmptyType)
    public:
      EmptyType(Context& context, const SourceLocation& location);
    };
    
    /**
     * \brief The unique value of the empty type.
     */
    class PSI_TVM_EXPORT_DEBUG EmptyValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(EmptyValue)
    public:
      EmptyValue(Context& context, const SourceLocation& location);
    };
    
    class PSI_TVM_EXPORT_DEBUG OuterPtr : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(OuterPtr)
      
    public:
      OuterPtr(const ValuePtr<>& pointer, const SourceLocation& location);
      
      /// \brief Get the pointer
      const ValuePtr<>& pointer() const {return m_pointer;}
      
    private:
      ValuePtr<> m_pointer;
    };

    /**
     * \brief Type of upward references.
     */
    class PSI_TVM_EXPORT_DEBUG UpwardReferenceType : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(UpwardReference)
      
    public:
      UpwardReferenceType(Context& context, const SourceLocation& location);
    };
    
    /**
     * \brief Upward reference value.
     */
    class PSI_TVM_EXPORT_DEBUG UpwardReference : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(UpwardReference)
      
    public:
      UpwardReference(const ValuePtr<>& outer_type, const ValuePtr<>& index, const ValuePtr<>& next, const SourceLocation& location);
      
      /// \brief The outer type of this reference.
      ValuePtr<> outer_type() const;
      /// \brief The outer type of this reference, if non-NULL, which it may be if next() is non-NULL.
      const ValuePtr<>& maybe_outer_type() const {return m_outer_type;}
      /// \brief The index into the outer type to which we have a pointer.
      const ValuePtr<>& index() const {return m_index;}
      /// \brief Next upward reference in the chain.
      const ValuePtr<>& next() const {return m_next;}
      
    private:
      ValuePtr<> m_outer_type;
      ValuePtr<> m_index;
      ValuePtr<> m_next;
      
      static void hashable_check_source(UpwardReference&, CheckSourceParameter&);
      virtual bool match_impl(const FunctionalValue& other, std::vector<ValuePtr<> >& parameters, unsigned depth, UprefMatchMode upref_mode) const;
    };
    
    class PSI_TVM_EXPORT_DEBUG UpwardReferenceNull : public HashableValue {
      PSI_TVM_HASHABLE_DECL(UpwardReferenceUnknown)
      
    public:
      UpwardReferenceNull(Context& context, const SourceLocation& location);
      static bool isa_impl(const Value& ptr) {return ptr.term_type() == term_upref_null;}
    };
    
    /**
     * \brief A type which can only have a single value.
     */
    class PSI_TVM_EXPORT_DEBUG ConstantType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(ConstantType)
    public:
      ConstantType(const ValuePtr<>& value, const SourceLocation& location);

      /// \brief Get the value of this type
      const ValuePtr<>& value() const {return m_value;}

    private:
      ValuePtr<> m_value;
    };
    
    /**
     * \brief The type of a BlockTerm.
     */
    class PSI_TVM_EXPORT_DEBUG BlockType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(BlockType)      
    public:
      BlockType(Context& context, const SourceLocation& location);
    };
    
    /**
     * \brief The smallest unit of storage in the system.
     * 
     * \c sizeof and \c alignof are measure in units of this type.
     */
    class PSI_TVM_EXPORT_DEBUG ByteType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(ByteType)
    public:
      ByteType(Context& context, const SourceLocation& location);
    };

    /**
     * \brief The type of every term which can be used as a type except itself.
     * 
     * This is the \c type term: all terms which can be used as types are
     * themselves of type \c type except \c type, which has no type.
     */
    class PSI_TVM_EXPORT_DEBUG Metatype : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(Metatype)
    public:
      Metatype(Context& context, const SourceLocation& location);
    };

    /**
     * \brief Generate a value for Metatype.
     *
     * This can be used by the user, however without pointer casts the
     * generated type will be no use. It is mostly present to support
     * code generation, where the other types use this to construct a
     * type.
     */
    class PSI_TVM_EXPORT_DEBUG MetatypeValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(MetatypeValue)
    public:
      
      MetatypeValue(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location);
      
      /// \brief Get the size of this type
      const ValuePtr<>& size() const {return m_size;}
      /// \brief Get the alignment of this type
      const ValuePtr<>& alignment() const {return m_alignment;}

    private:
      ValuePtr<> m_size;
      ValuePtr<> m_alignment;
    };
    
    /// Operation to get the size of a type, like \c sizeof in C.
    PSI_TVM_UNARY_OP_DECL(MetatypeSize, UnaryOp);
    /// Operation to the the alignment of a type, like \c alignof in C99
    PSI_TVM_UNARY_OP_DECL(MetatypeAlignment, UnaryOp);
    
    /**
     * \brief An undefined value.
     * 
     * This is a valid value for any type, and can be freely replaced
     * by any other value whatsoever during compilation. Note that while
     * two terms which contain undefined values may be pointer-wise equal,
     * they may not evaluate to the same value at run-time (since they
     * are not completely well defined).
     */
    PSI_TVM_UNARY_OP_DECL(UndefinedValue, UnaryOp)
    
    /**
     * \brief Zero-initialized value.
     * 
     * A value of any type which is zero.
     */
    PSI_TVM_UNARY_OP_DECL(ZeroValue, UnaryOp)

    /**
     * \brief A pointer type.
     */
    class PSI_TVM_EXPORT_DEBUG PointerType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(PointerType)
    public:
      PointerType(const ValuePtr<>& target_type, const ValuePtr<>& upref, const SourceLocation& location);

      /// \brief Get the type being pointed to.
      const ValuePtr<>& target_type() const {return m_target_type;}
      const ValuePtr<>& upref() const {return m_upref;}
      
    private:
      ValuePtr<> m_target_type;
      ValuePtr<> m_upref;

      static void hashable_check_source(PointerType& self, CheckSourceParameter& parameter);
      virtual bool match_impl(const FunctionalValue& other, std::vector<ValuePtr<> >& parameters, unsigned depth, UprefMatchMode upref_mode) const;
    };
    
    /**
     * \brief Get the value type of a global, i.e. the type pointed to
     * by the global's value.
     */
    inline ValuePtr<> Global::value_type() const {
      return value_cast<PointerType>(type())->target_type();
    }

    /**
     * \brief Cast a pointer from one type to another while keeping its address the same.
     */
    class PSI_TVM_EXPORT_DEBUG PointerCast : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(PointerCast)
    public:
      PointerCast(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const ValuePtr<>& upref, const SourceLocation& location);

      /// \brief Get the pointer being cast
      const ValuePtr<>& pointer() const {return m_pointer;}
      /// \brief Get the target type of the cast
      const ValuePtr<>& target_type() const {return m_target_type;}
      /// \brief Get the upward reference type to cast to.
      const ValuePtr<>& upref() const {return m_upref;}
      
    private:
      ValuePtr<> m_pointer;
      ValuePtr<> m_target_type;
      ValuePtr<> m_upref;
    };
    
    /**
     * \brief Change the memory address pointed to by a pointer.
     * 
     * The \c offset parameter to this operation must be an intptr
     * or uintptr - exactly how the operation works depends on
     * whether it is signed or unsigned. The \c offset parameter
     * is measured in units of the pointed-to type.
     */
    class PSI_TVM_EXPORT_DEBUG PointerOffset : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(PointerOffset)
    public:
      PointerOffset(const ValuePtr<>& pointer, const ValuePtr<>& offset, const SourceLocation& location);

      /// \brief Get the original pointer
      const ValuePtr<>& pointer() const {return m_pointer;}
      /// \brief Get the offset
      const ValuePtr<>& offset() const {return m_offset;}
      /// \brief Get the type of this pointer, cast to a pointer type.
      ValuePtr<PointerType> pointer_type() const {return value_cast<PointerType>(m_pointer->type());}
      /// \brief Get the target type of this operations type.
      const ValuePtr<>& target_type() const {return value_cast<PointerType>(Value::type().get())->target_type();}

    private:
      ValuePtr<> m_pointer;
      ValuePtr<> m_offset;
    };

    /**
     * \brief An array type - a collection of identical elements of fixed length.
     */
    class PSI_TVM_EXPORT_DEBUG ArrayType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(ArrayType)
    public:
      ArrayType(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location);

      /// \brief Get the array element type.
      const ValuePtr<>& element_type() const {return m_element_type;}
      /// \brief Get the array length.
      const ValuePtr<>& length() const {return m_length;}

    private:
      ValuePtr<> m_element_type;
      ValuePtr<> m_length;
      
      virtual bool match_impl(const FunctionalValue& other, std::vector<ValuePtr<> >& parameters, unsigned depth, UprefMatchMode upref_mode) const;
    };

    /**
     * \brief Constructs a value for ArrayType from a list of element values.
     */
    class PSI_TVM_EXPORT_DEBUG ArrayValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(ArrayValue)
      
    public:
      ArrayValue(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location);

      /// \brief Get the element type in the array.
      const ValuePtr<>& element_type() const {return m_element_type;}
      /// \brief Get the length of the array value.
      unsigned length() const {return m_elements.size();}
      /// \brief Get the value of the specified array element.
      const ValuePtr<>& value(std::size_t n) const {return m_elements[n];}
      /// \brief Get the type of this value (overloaded to return an ArrayType).
      ValuePtr<ArrayType> type() const {return value_cast<ArrayType>(Value::type());}
      
    private:
      ValuePtr<> m_element_type;
      std::vector<ValuePtr<> > m_elements;
    };

    /**
     * \brief The struct type, which contains a series of values of different types.
     */
    class PSI_TVM_EXPORT_DEBUG StructType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(StructType)
    public:
      StructType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);

      /// \brief Get the number of members in the aggregate.
      unsigned n_members() const {return m_members.size();}
      /// \brief Get the type of the given member.
      const ValuePtr<>& member_type(std::size_t i) const {return m_members[i];}
      
    private:
      std::vector<ValuePtr<> > m_members;

      virtual bool match_impl(const FunctionalValue& other, std::vector<ValuePtr<> >& parameters, unsigned depth, UprefMatchMode upref_mode) const;
    };

    /**
     * \brief Constructs a value of type StructType from the values of the individual elements.
     */
    class PSI_TVM_EXPORT_DEBUG StructValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(StructValue)
    public:
      StructValue(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);

      /// \brief Get the number of elements in the struct.
      unsigned n_members() const {return m_members.size();}
      /// \brief Get the value of the specified member.
      const ValuePtr<>& member_value(std::size_t n) const {return m_members[n];}
      /// \brief Get the type of this value (overloaded to return a StructType).
      ValuePtr<StructType> type() const {return value_cast<StructType>(Value::type());}
      
    private:
      std::vector<ValuePtr<> > m_members;
    };

    /**
     * \brief Get the offset of a struct member
     * 
     * This should be considered an internal operation, and not be
     * created by the user.
     */
    class PSI_TVM_EXPORT_DEBUG StructElementOffset : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(StructElementOffset)
    public:
      StructElementOffset(const ValuePtr<>& struct_type, unsigned index, const SourceLocation& location);

      /// \brief Get the struct type
      const ValuePtr<>& struct_type() const {return m_struct_type;}
      /// \brief Get the index of the member we're getting the offset of
      unsigned index() const {return m_index;}
      
    private:
      ValuePtr<> m_struct_type;
      unsigned m_index;
    };

    /**
     * \brief The union type - a type which holds a value of one type
     * from a set of possible types.
     */
    class PSI_TVM_EXPORT_DEBUG UnionType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(UnionType)
    public:
      UnionType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);

      /// \brief Get the number of members in the aggregate.
      unsigned n_members() const {return m_members.size();}
      /// \brief Get the type of the given member.
      const ValuePtr<>& member_type(unsigned i) const {return m_members[i];}
      PSI_TVM_EXPORT int index_of_type(const ValuePtr<>& type) const;
      PSI_TVM_EXPORT bool contains_type(const ValuePtr<>& type) const;
      
    private:
      std::vector<ValuePtr<> > m_members;

      virtual bool match_impl(const FunctionalValue& other, std::vector<ValuePtr<> >& parameters, unsigned depth, UprefMatchMode upref_mode) const;
    };

    /**
     * \brief Constructs a value for UnionType from the type plus the value
     * of the member this particular instance is holding.
     */
    class PSI_TVM_EXPORT_DEBUG UnionValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(UnionValue)
    public:
      UnionValue(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location);

      /// \brief Get the value of whichever element this represents.
      const ValuePtr<>& value() const {return m_value;}
      /// \brief Get the type of this value (overloaded to return a UnionType).
      ValuePtr<UnionType> union_type() const {return value_cast<UnionType>(Value::type());}
      
    private:
      ValuePtr<> m_union_type;
      ValuePtr<> m_value;
    };
    
    class PSI_TVM_EXPORT_DEBUG ApplyValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(ApplyValue)
    public:
      ApplyValue(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location);
      
      ValuePtr<ApplyType> apply_type() const;
      /// \brief Get the value used to construct the generic
      const ValuePtr<>& value() const {return m_value;}
      
    private:
      ValuePtr<> m_apply_type;
      ValuePtr<> m_value;
    };

    /**
     * \brief Get the value of an element of an aggregate from an aggregate value.
     */
    class PSI_TVM_EXPORT_DEBUG ElementValue : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(ElementValue)
    public:
      ElementValue(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location);

      /// \brief Get the aggregate being accessed
      const ValuePtr<>& aggregate() const {return m_aggregate;}
      /// \brief Get the index of the member being accessed
      const ValuePtr<>& index() const {return m_index;}
      
    private:
      ValuePtr<> m_aggregate;
      ValuePtr<> m_index;
    };

    /**
     * \brief Gets a pointer to a member of a union from a
     * pointer to the union.
     * 
     * This operation doesn't really need to exist: it's
     * entirely equivalent to PointerCast, but I put it in
     * because it's semantically different.
     */
    class PSI_TVM_EXPORT_DEBUG ElementPtr : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(ElementPtr)
    public:
      ElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location);

      /// \brief Get the aggregate being accessed
      const ValuePtr<>& aggregate_ptr() const {return m_aggregate_ptr;}
      /// \brief Index of the member being retrieved.
      const ValuePtr<>& index() const {return m_index;}
      
    private:
      ValuePtr<> m_aggregate_ptr;
      ValuePtr<> m_index;
    };
    
    /**
     * \brief Specialize a function pointer.
     * 
     * Function types in Tvm can include &lsquo;ghost&rdquo; terms,
     * which are only used to track type information and are not actually
     * passed to the target function. A function pointer of a simpler
     * type with fewer ghost terms can be generated using this operation.
     * Note that this is not like wrapping a function in another function:
     * it does not adjust the pointer, merely the type.
     */
    class PSI_TVM_EXPORT_DEBUG FunctionSpecialize : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(FunctionSpecialize)
    public:
      FunctionSpecialize(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);

      /// \brief Get the function being specialized.
      const ValuePtr<>& function() const {return m_function;}
      /// \brief Get the number of parameters being applied.
      unsigned n_parameters() const {return m_parameters.size();}
      /// \brief Get the value of the <tt>n</tt>th parameter being applied.
      const ValuePtr<>& parameter(std::size_t n) const {return m_parameters[n];}
      /// \brief Get the type of this value (overloaded to return a PointerType).
      ValuePtr<PointerType> type() const {return value_cast<PointerType>(Value::type());}
      /// \brief Get the function type of this term (the type of this
      /// term is a pointer to this type).
      ValuePtr<FunctionType> function_type() const {
        return value_cast<FunctionType>(type()->target_type());
      }
      
    private:
      ValuePtr<> m_function;
      std::vector<ValuePtr<> > m_parameters;
    };
  }
}

#endif
