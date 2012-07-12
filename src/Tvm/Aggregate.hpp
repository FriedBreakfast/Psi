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
    class EmptyType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(EmptyType)
    public:
      EmptyType(Context& context, const SourceLocation& location);
      static ValuePtr<EmptyType> get(Context& context, const SourceLocation& location);
    };
    
    /**
     * \brief The unique value of the empty type.
     */
    class EmptyValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(EmptyValue)
    public:
      EmptyValue(Context& context, const SourceLocation& location);
      static ValuePtr<EmptyValue> get(Context& context, const SourceLocation& location);
    };
    
    /**
     * \brief The type of a BlockTerm.
     */
    class BlockType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(BlockType)      
    public:
      BlockType(Context& context, const SourceLocation& location);
      static ValuePtr<BlockType> get(Context& context, const SourceLocation& location);
    };
    
    /**
     * \brief The smallest unit of storage in the system.
     * 
     * \c sizeof and \c alignof are measure in units of this type.
     */
    class ByteType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(ByteType)
    public:
      ByteType(Context& context, const SourceLocation& location);
      static ValuePtr<ByteType> get(Context& context, const SourceLocation& location);
    };

    /**
     * \brief The type of every term which can be used as a type except itself.
     * 
     * This is the \c type term: all terms which can be used as types are
     * themselves of type \c type except \c type, which has no type.
     */
    class Metatype : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(Metatype)
    public:
      Metatype(Context& context, const SourceLocation& location);
      static ValuePtr<> get(Context& context, const SourceLocation& location);
    };

    /**
     * \brief Generate a value for Metatype.
     *
     * This can be used by the user, however without pointer casts the
     * generated type will be no use. It is mostly present to support
     * code generation, where the other types use this to construct a
     * type.
     */
    class MetatypeValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(MetatypeValue)
    public:
      
      MetatypeValue(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location);
      
      /// \brief Get the size of this type
      const ValuePtr<>& size() const {return m_size;}
      /// \brief Get the alignment of this type
      const ValuePtr<>& alignment() const {return m_alignment;}

    private:
      ValuePtr<> m_size;
      ValuePtr<> m_alignment;
    };
    
    /// Operation to get the size of a type, like \c sizeof in C.
    class MetatypeSize : public UnaryOp {
      PSI_TVM_FUNCTIONAL_DECL(MetatypeSize)
    public:
      MetatypeSize(const ValuePtr<>& target, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& target, const SourceLocation& location);
    };
    
    /// Operation to the the alignment of a type, like \c alignof in C99
    class MetatypeAlignment : public UnaryOp {
      PSI_TVM_FUNCTIONAL_DECL(MetatypeAlignment)
    public:
      MetatypeAlignment(const ValuePtr<>& target, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& target, const SourceLocation& location);
    };
    
    /**
     * \brief An undefined value.
     * 
     * This is a valid value for any type, and can be freely replaced
     * by any other value whatsoever during compilation. Note that while
     * two terms which contain undefined values may be pointer-wise equal,
     * they may not evaluate to the same value at run-time (since they
     * are not completely well defined).
     */
    class UndefinedValue : public SimpleOp {
      PSI_TVM_FUNCTIONAL_DECL(UndefinedValue)
    public:
      UndefinedValue(const ValuePtr<>& type, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& type, const SourceLocation& location);
    };

    /**
     * \brief A pointer type.
     */
    class PointerType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(PointerType)
    public:
      PointerType(const ValuePtr<>& target_type, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& target_type, const SourceLocation& location);

      /// \brief Get the type being pointed to.
      const ValuePtr<>& target_type() const {return m_target_type;}

    private:
      ValuePtr<> m_target_type;
    };

    /**
     * \brief Cast a pointer from one type to another while keeping its address the same.
     */
    class PointerCast : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(PointerCast)
    public:
      PointerCast(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& pointer, const ValuePtr<>& target_type, const SourceLocation& location);

      /// \brief Get the pointer being cast
      const ValuePtr<>& pointer() const {return m_pointer;}
      /// \brief Get the target type of the cast
      const ValuePtr<>& target_type() const {return m_target_type;}
      
    private:
      ValuePtr<> m_pointer;
      ValuePtr<> m_target_type;
    };
    
    /**
     * \brief Change the memory address pointed to by a pointer.
     * 
     * The \c offset parameter to this operation must be an intptr
     * or uintptr - exactly how the operation works depends on
     * whether it is signed or unsigned. The \c offset parameter
     * is measured in units of the pointed-to type.
     */
    class PointerOffset : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(PointerOffset)
    public:
      PointerOffset(const ValuePtr<>& pointer, const ValuePtr<>& offset, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& pointer, const ValuePtr<>& offset, const SourceLocation& location);

      /// \brief Get the original pointer
      const ValuePtr<>& pointer() const {return m_pointer;}
      /// \brief Get the offset
      const ValuePtr<>& offset() const {return m_offset;}
      /// \brief Get the type of this pointer, cast to a pointer type.
      ValuePtr<PointerType> type() const {return value_cast<PointerType>(m_pointer);}
      /// \brief Get the target type of this operations type.
      const ValuePtr<>& target_type() const {return value_cast<PointerType>(Value::type().get())->target_type();}

    private:
      ValuePtr<> m_pointer;
      ValuePtr<> m_offset;
    };

    /**
     * \brief An array type - a collection of identical elements of fixed length.
     */
    class ArrayType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(ArrayType)
    public:
      ArrayType(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location);

      /// \brief Get the array element type.
      const ValuePtr<>& element_type() const {return m_element_type;}
      /// \brief Get the array length.
      const ValuePtr<>& length() const {return m_length;}

    private:
      ValuePtr<> m_element_type;
      ValuePtr<> m_length;
    };

    /**
     * \brief Constructs a value for ArrayType from a list of element values.
     */
    class ArrayValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(ArrayValue)
      
    public:
      ArrayValue(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& element_type, const std::vector<ValuePtr<> >& elements, const SourceLocation& location);
      
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

    /// \brief Get the value of an array element
    class ArrayElement : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(ArrayElement)
    public:
      ArrayElement(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate, const ValuePtr<>& index, const SourceLocation& location);

      /// \brief Get the array being subscripted
      const ValuePtr<>& aggregate() const {return m_aggregate;}
      /// \brief Get the index
      const ValuePtr<>& index() const {return m_index;}
      /// \brief Get the type of array being accessed
      ValuePtr<ArrayType> aggregate_type() const {return value_cast<ArrayType>(aggregate()->type());}
      
      static ValuePtr<> get(const ValuePtr<>& aggregate, const ValuePtr<>& index);
      
    private:
      ValuePtr<> m_aggregate;
      ValuePtr<> m_index;
    };

    /**
     * \brief Get a pointer to an element of an array
     * from a pointer to an array.
     */
    class ArrayElementPtr : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(ArrayElementPtr)
    public:
      ArrayElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index, const SourceLocation& location);

      /// \brief Get the array being subscripted
      const ValuePtr<>& aggregate_ptr() const {return m_aggregate_ptr;}
      /// \brief Get the index
      const ValuePtr<>& index() const {return m_index;}
      /// \brief Get the array type being subscripted
      ValuePtr<ArrayType> aggregate_type() const {return value_cast<ArrayType>(value_cast<PointerType>(aggregate_ptr()->type().get())->target_type());}
      
      static ValuePtr<ArrayElementPtr> get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& index);
      
    private:
      ValuePtr<> m_aggregate_ptr;
      ValuePtr<> m_index;
    };

    /**
     * \brief The struct type, which contains a series of values of different types.
     */
    class StructType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(StructType)
    public:
      StructType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);
      static ValuePtr<StructType> get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);

      /// \brief Get the number of members in the aggregate.
      unsigned n_members() const {return m_members.size();}
      /// \brief Get the type of the given member.
      const ValuePtr<>& member_type(std::size_t i) const {return m_members[i];}

      static ValuePtr<StructType> get(Context& context, const std::vector<ValuePtr<> >& elements);
      
    private:
      std::vector<ValuePtr<> > m_members;
    };

    /**
     * \brief Constructs a value of type StructType from the values of the individual elements.
     */
    class StructValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(StructValue)
    public:
      StructValue(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);
      static ValuePtr<> get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);

      /// \brief Get the number of elements in the struct.
      unsigned n_members() const {return m_members.size();}
      /// \brief Get the value of the specified member.
      const ValuePtr<>& member_value(std::size_t n) const {return m_members[n];}
      /// \brief Get the type of this value (overloaded to return a StructType).
      ValuePtr<StructType> type() const {return value_cast<StructType>(Value::type());}

      static ValuePtr<StructValue> get(Context& context, const std::vector<ValuePtr<> >& elements);
      
    private:
      std::vector<ValuePtr<> > m_members;
    };

    /// \brief Get the value of a struct member
    class StructElement : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(StructElement)
    public:
      StructElement(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate, unsigned index, const SourceLocation& location);

      /// \brief Get the struct being accessed
      const ValuePtr<>& aggregate() const {return m_aggregate;}
      /// \brief Get the index of the member being accessed
      unsigned index() const {return m_index;}
      /// \brief Get the type of struct being accessed
      ValuePtr<StructType> aggregate_type() const {return value_cast<StructType>(aggregate()->type());}
      
      static ValuePtr<StructElement> get(const ValuePtr<>& structure, unsigned index);

    private:
      ValuePtr<> m_aggregate;
      unsigned m_index;
    };

    /**
     * \brief Get a pointer to a member of a struct from a
     * pointer to the struct.
     */
    class StructElementPtr : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(StructElementPtr)
    public:
      StructElementPtr(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate_ptr, unsigned index, const SourceLocation& location);
      
      /// \brief Get the struct being subscripted
      const ValuePtr<>& aggregate_ptr() const {return m_aggregate_ptr;}
      /// \brief Get the index
      unsigned index() const {return m_index;}
      /// \brief Get the struct type being subscripted
      ValuePtr<StructType> aggregate_type() const {return value_cast<StructType>(value_cast<PointerType>(aggregate_ptr()->type().get())->target_type());}

      static ValuePtr<StructElementPtr> get(const ValuePtr<>& aggregate_ptr, unsigned index);
      
    private:
      ValuePtr<> m_aggregate_ptr;
      unsigned m_index;
    };

    /**
     * \brief Get the offset of a struct member
     * 
     * This should be considered an internal operation, and not be
     * created by the user.
     */
    class StructElementOffset : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(StructElementOffset)
    public:
      StructElementOffset(const ValuePtr<>& aggregate_type, unsigned index, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate_type, unsigned index, const SourceLocation& location);

      /// \brief Get the struct type
      const ValuePtr<StructType>& aggregate_type() const {return m_aggregate_type;}
      /// \brief Get the index of the member we're getting the offset of
      unsigned index() const {return m_index;}
      
      static ValuePtr<StructElementOffset> get(const ValuePtr<StructType>& aggregate_type, unsigned index);
      
    private:
      ValuePtr<StructType> m_aggregate_type;
      unsigned m_index;
    };

    /**
     * \brief The union type - a type which holds a value of one type
     * from a set of possible types.
     */
    class UnionType : public Type {
      PSI_TVM_FUNCTIONAL_DECL(UnionType)
    public:
      UnionType(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);
      static ValuePtr<> get(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);

      /// \brief Get the number of members in the aggregate.
      unsigned n_members() const {return m_members.size();}
      /// \brief Get the type of the given member.
      const ValuePtr<>& member_type(unsigned i) const {return m_members[i];}
      int index_of_type(const ValuePtr<>& type) const;
      bool contains_type(const ValuePtr<>& type) const;

      static ValuePtr<UnionType> get(Context& context, const std::vector<ValuePtr<> >& elements);
      
    private:
      std::vector<ValuePtr<> > m_members;
    };

    /**
     * \brief Constructs a value for UnionType from the type plus the value
     * of the member this particular instance is holding.
     */
    class UnionValue : public Constructor {
      PSI_TVM_FUNCTIONAL_DECL(UnionValue)
    public:
      UnionValue(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& type, const ValuePtr<>& value, const SourceLocation& location);

      /// \brief Get the value of whichever element this represents.
      const ValuePtr<>& value() const {return m_value;}
      /// \brief Get the type of this value (overloaded to return a UnionType).
      ValuePtr<UnionType> type() const {return value_cast<UnionType>(Value::type());}

      static ValuePtr<UnionValue> get(const ValuePtr<UnionType>& union_type, const ValuePtr<>& value);
      
    private:
      ValuePtr<> m_value;
    };

    /// \brief Get the value of a union member
    class UnionElement : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(UnionElement)
    public:
      UnionElement(const ValuePtr<>& aggregate, const ValuePtr<>& member_type, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate, const ValuePtr<>& member_type, const SourceLocation& location);

      /// \brief Get the union being accessed
      const ValuePtr<>& aggregate() const {return m_aggregate;}
      /// \brief Get the type of the member being accessed
      const ValuePtr<>& member_type() const {return m_member_type;}
      
      static ValuePtr<UnionElement> get(const ValuePtr<>& aggregate, const ValuePtr<>& member_type);
      
    private:
      ValuePtr<> m_aggregate;
      ValuePtr<> m_member_type;
    };

    /**
     * \brief Gets a pointer to a member of a union from a
     * pointer to the union.
     * 
     * This operation doesn't really need to exist: it's
     * entirely equivalent to PointerCast, but I put it in
     * because it's semantically different.
     */
    class UnionElementPtr : public AggregateOp {
      PSI_TVM_FUNCTIONAL_DECL(UnionElementPtr)
    public:
      UnionElementPtr(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type, const SourceLocation& location);

      /// \brief Get the struct being subscripted
      const ValuePtr<>& aggregate_ptr() const {return m_aggregate_ptr;}
      /// \brief Get the index
      const ValuePtr<>& member_type() const {return m_member_type;}
      /// \brief Get the union type being subscripted
      ValuePtr<UnionType> aggregate_type() const {return value_cast<UnionType>(value_cast<PointerType>(aggregate_ptr()->type())->target_type());}

      static ValuePtr<UnionElementPtr> get(const ValuePtr<>& aggregate_ptr, const ValuePtr<>& member_type);
      
    private:
      ValuePtr<> m_aggregate_ptr;
      ValuePtr<> m_member_type;
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
    class FunctionSpecialize : public FunctionalValue {
      PSI_TVM_FUNCTIONAL_DECL(FunctionSpecialize)
    public:
      FunctionSpecialize(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);
      static ValuePtr<> get(const ValuePtr<>& function, const std::vector<ValuePtr<> >& parameters, const SourceLocation& location);

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
