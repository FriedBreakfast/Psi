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
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(EmptyType)
    
    /**
     * \brief The unique value of the empty type.
     */
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(EmptyValue)
    
    /**
     * \brief The type of a BlockTerm.
     */
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(BlockType)
    
    /**
     * \brief The smallest unit of storage in the system.
     * 
     * \c sizeof and \c alignof are measure in units of this type.
     */
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(ByteType)

    /**
     * \brief The type of every term which can be used as a type except itself.
     * 
     * This is the \c type term: all terms which can be used as types are
     * themselves of type \c type except \c type, which has no type.
     */
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(Metatype)

    /**
     * \brief Generate a value for Metatype.
     *
     * This can be used by the user, however without pointer casts the
     * generated type will be no use. It is mostly present to support
     * code generation, where the other types use this to construct a
     * type.
     */
    PSI_TVM_FUNCTIONAL_TYPE(MetatypeValue, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the size of this type
    Term *size() const {return get()->parameter(0);}
    /// \brief Get the alignment of this type
    Term *alignment() const {return get()->parameter(1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *size, Term *alignment);
    PSI_TVM_FUNCTIONAL_TYPE_END(MetatypeValue)

    /// Operation to get the size of a type, like \c sizeof in C.
    PSI_TVM_FUNCTIONAL_TYPE_UNARY(MetatypeSize, Term)

    /// Operation to the the alignment of a type, like \c alignof in C99
    PSI_TVM_FUNCTIONAL_TYPE_UNARY(MetatypeAlignment, Term)
    
    /**
     * \brief An undefined value.
     * 
     * This is a valid value for any type, and can be freely replaced
     * by any other value whatsoever during compilation. Note that while
     * two terms which contain undefined values may be pointer-wise equal,
     * they may not evaluate to the same value at run-time (since they
     * are not completely well defined).
     */
    PSI_TVM_FUNCTIONAL_TYPE(UndefinedValue, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *type);
    PSI_TVM_FUNCTIONAL_TYPE_END(UndefinedValue)

    /**
     * \brief A pointer type.
     */
    PSI_TVM_FUNCTIONAL_TYPE(PointerType, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the type being pointed to.
    Term* target_type() const {return get()->parameter(0);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *target_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(PointerType)

    /**
     * \brief Cast a pointer from one type to another while keeping its address the same.
     */
    PSI_TVM_FUNCTIONAL_TYPE(PointerCast, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the pointer being cast
    Term *pointer() const {return get()->parameter(0);}
    /// \brief Get the target type of the cast
    Term *target_type() const {return get()->parameter(1);}
    /// \brief Get the type of this cast, cast to a pointer type.
    PointerType::Ptr type() const {return cast<PointerType>(PtrBaseType::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *pointer, Term *target_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(PointerCast)
    
    /**
     * \brief Change the memory address pointed to by a pointer.
     * 
     * The \c offset parameter to this operation must be an intptr
     * or uintptr - exactly how the operation works depends on
     * whether it is signed or unsigned. The \c offset parameter
     * is measured in units of the pointed-to type.
     */
    PSI_TVM_FUNCTIONAL_TYPE(PointerOffset, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the original pointer
    Term *pointer() const {return get()->parameter(0);}
    /// \brief Get the offset
    Term *offset() const {return get()->parameter(1);}
    /// \brief Get the type of this cast, cast to a pointer type.
    PointerType::Ptr type() const {return cast<PointerType>(PtrBaseType::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *pointer, Term *offset);
    PSI_TVM_FUNCTIONAL_TYPE_END(PointerOffset)

    /**
     * \brief An array type - a collection of identical elements of fixed length.
     */
    PSI_TVM_FUNCTIONAL_TYPE(ArrayType, TypeOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the array element type.
    Term* element_type() const {return get()->parameter(0);}
    /// \brief Get the array length.
    Term* length() const {return get()->parameter(1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *element_type, Term *length);
    static Ptr get(Term *element_type, unsigned length);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayType)

    /**
     * \brief Constructs a value for ArrayType from a list of element values.
     */
    PSI_TVM_FUNCTIONAL_TYPE(ArrayValue, ConstructorOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the element type in the array.
    Term* element_type() const {return get()->parameter(0);}
    /// \brief Get the length of the array value.
    unsigned length() const {return get()->n_parameters() - 1;}
    /// \brief Get the value of the specified array element.
    Term* value(std::size_t n) const {return get()->parameter(n+1);}
    /// \brief Get the type of this value (overloaded to return an ArrayType).
    ArrayType::Ptr type() const {return cast<ArrayType>(TermPtrBase::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *element_type, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayValue)

    /// \brief Get the value of an array element
    PSI_TVM_FUNCTIONAL_TYPE(ArrayElement, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the array being subscripted
    Term *aggregate() const {return get()->parameter(0);}
    /// \brief Get the index
    Term *index() const {return get()->parameter(1);}
    /// \brief Get the type of array being accessed
    ArrayType::Ptr aggregate_type() const {return cast<ArrayType>(aggregate()->type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate, Term *index);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayElement)

    /**
     * \brief Get a pointer to an element of an array
     * from a pointer to an array.
     */
    PSI_TVM_FUNCTIONAL_TYPE(ArrayElementPtr, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the array being subscripted
    Term *aggregate_ptr() const {return get()->parameter(0);}
    /// \brief Get the index
    Term *index() const {return get()->parameter(1);}
    /// \brief Get the array type being subscripted
    ArrayType::Ptr aggregate_type() const {return cast<ArrayType>(cast<PointerType>(aggregate_ptr()->type())->target_type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate_ptr, Term *index);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayElementPtr)

    /**
     * \brief The struct type, which contains a series of values of different types.
     */
    PSI_TVM_FUNCTIONAL_TYPE(StructType, TypeOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of members in the aggregate.
    unsigned n_members() const {return get()->n_parameters();}
    /// \brief Get the type of the given member.
    Term* member_type(std::size_t i) const {return get()->parameter(i);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructType)

    /**
     * \brief Constructs a value of type StructType from the values of the individual elements.
     */
    PSI_TVM_FUNCTIONAL_TYPE(StructValue, ConstructorOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of elements in the struct.
    unsigned n_members() const {return get()->n_parameters();}
    /// \brief Get the value of the specified member.
    Term* member_value(std::size_t n) const {return get()->parameter(n);}
    /// \brief Get the type of this value (overloaded to return a StructType).
    StructType::Ptr type() const {return cast<StructType>(TermPtrBase::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructValue)

    /// \brief Get the value of a struct member
    PSI_TVM_FUNCTIONAL_TYPE(StructElement, FunctionalOperation)
    typedef PrimitiveWrapper<unsigned> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the struct being accessed
    Term *aggregate() const {return get()->parameter(0);}
    /// \brief Get the index of the member being accessed
    unsigned index() const {return data().value();}
    /// \brief Get the type of struct being accessed
    StructType::Ptr aggregate_type() const {return cast<StructType>(aggregate()->type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *structure, unsigned index);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructElement)

    /**
     * \brief Get a pointer to a member of a struct from a
     * pointer to the struct.
     */
    PSI_TVM_FUNCTIONAL_TYPE(StructElementPtr, FunctionalOperation)
    typedef PrimitiveWrapper<unsigned> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the struct being subscripted
    Term *aggregate_ptr() const {return get()->parameter(0);}
    /// \brief Get the index
    unsigned index() const {return data().value();}
    /// \brief Get the struct type being subscripted
    StructType::Ptr aggregate_type() const {return cast<StructType>(cast<PointerType>(aggregate_ptr()->type())->target_type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate_ptr, unsigned index);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructElementPtr)

    /**
     * \brief Get the offset of a struct member
     * 
     * This should be considered an internal operation, and not be
     * created by the user.
     */
    PSI_TVM_FUNCTIONAL_TYPE(StructElementOffset, FunctionalOperation)
    typedef PrimitiveWrapper<unsigned> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the struct type
    StructType::Ptr aggregate_type() const {return cast<StructType>(get()->parameter(0));}
    /// \brief Get the index of the member we're getting the offset of
    unsigned index() const {return data().value();}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *struct_type, unsigned index);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructElementOffset)

    /**
     * \brief The union type - a type which holds a value of one type
     * from a set of possible types.
     */
    PSI_TVM_FUNCTIONAL_TYPE(UnionType, TypeOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of members in the aggregate.
    unsigned n_members() const {return get()->n_parameters();}
    /// \brief Get the type of the given member.
    Term* member_type(unsigned i) const {return get()->parameter(i);}
    int index_of_type(Term *type) const;
    bool contains_type(Term *type) const;
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionType)

    /**
     * \brief Constructs a value for UnionType from the type plus the value
     * of the member this particular instance is holding.
     */
    PSI_TVM_FUNCTIONAL_TYPE(UnionValue, ConstructorOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of whichever element this represents.
    Term* value() const {return get()->parameter(1);}
    /// \brief Get the type of this value (overloaded to return a UnionType).
    UnionType::Ptr type() const {return cast<UnionType>(TermPtrBase::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term* type, Term *value);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionValue)

    /// \brief Get the value of a union member
    PSI_TVM_FUNCTIONAL_TYPE(UnionElement, FunctionalOperation)
    typedef unsigned Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the union being accessed
    Term *aggregate() const {return get()->parameter(0);}
    /// \brief Get the type of the member being accessed
    Term *member_type() const {return get()->parameter(1);}
    /// \brief Get the union type being subscripted
    UnionType::Ptr aggregate_type() const {return cast<UnionType>(aggregate()->type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate, Term *member_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionElement)

    /**
     * \brief Gets a pointer to a member of a union from a
     * pointer to the union.
     * 
     * This operation doesn't really need to exist: it's
     * entirely equivalent to PointerCast, but I put it in
     * because it's semantically different.
     */
    PSI_TVM_FUNCTIONAL_TYPE(UnionElementPtr, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the struct being subscripted
    Term *aggregate_ptr() const {return get()->parameter(0);}
    /// \brief Get the index
    Term *member_type() const {return get()->parameter(1);}
    /// \brief Get the union type being subscripted
    UnionType::Ptr aggregate_type() const {return cast<UnionType>(cast<PointerType>(aggregate_ptr()->type())->target_type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate_ptr, Term *member_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionElementPtr)

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
    PSI_TVM_FUNCTIONAL_TYPE(FunctionSpecialize, FunctionalOperation)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the function being specialized.
    Term* function() const {return get()->parameter(0);}
    /// \brief Get the number of parameters being applied.
    unsigned n_parameters() const {return get()->n_parameters() - 1;}
    /// \brief Get the value of the <tt>n</tt>th parameter being applied.
    Term* parameter(std::size_t n) const {return get()->parameter(n+1);}
    /// \brief Get the type of this value (overloaded to return a PointerType).
    PointerType::Ptr type() const {return cast<PointerType>(TermPtrBase::type());}
    /// \brief Get the function type of this term (the type of this
    /// term is a pointer to this type).
    FunctionTypeTerm* function_type() const {
      return cast<FunctionTypeTerm>(type()->target_type());
    }
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    PSI_TVM_FUNCTIONAL_TYPE_END(FunctionSpecialize)
  }
}

#endif
