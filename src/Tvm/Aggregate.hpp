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
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(EmptyType)
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(EmptyValue)
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(BlockType)

    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(Metatype)

    /**
     * Generate a value for Metatype.
     *
     * This can be used by the user, however without pointer casts the
     * generated type will be no use. It is mostly present to support
     * code generation, where the other types use this to construct a
     * type.
     */
    PSI_TVM_FUNCTIONAL_TYPE(MetatypeValue)
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
    PSI_TVM_FUNCTIONAL_TYPE(MetatypeSize)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the target of this instruction
    Term* target() const {return get()->parameter(0);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *target);
    PSI_TVM_FUNCTIONAL_TYPE_END(MetatypeSize)

    /// Operation to the the alignment of a type, like \c alignof in C99
    PSI_TVM_FUNCTIONAL_TYPE(MetatypeAlignment)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the target of this instruction
    Term* target() const {return get()->parameter(0);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *target);
    PSI_TVM_FUNCTIONAL_TYPE_END(MetatypeAlignment)

    PSI_TVM_FUNCTIONAL_TYPE(PointerType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the type being pointed to.
    Term* target_type() const {return get()->parameter(0);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *target_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(PointerType)

    PSI_TVM_FUNCTIONAL_TYPE(PointerCast)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the pointer being cast
    Term *pointer() const {return get()->parameter(0);}
    /// \brief Get the target type of the cast
    Term *target_type() const {return get()->parameter(1);}
    /// \brief Get the type of this cast, cast to a pointer type.
    PointerType::Ptr type() const {return cast<PointerType>(BaseType::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *pointer, Term *target_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(PointerCast)

    PSI_TVM_FUNCTIONAL_TYPE(ArrayType)
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

    PSI_TVM_FUNCTIONAL_TYPE(ArrayValue)
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
    PSI_TVM_FUNCTIONAL_TYPE(ArrayElement)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the array being indexed
    Term *aggregate() const {return get()->parameter(0);}
    /// \brief Get the index
    Term *index() const {return get()->parameter(1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate, Term *index);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayElement)

    PSI_TVM_FUNCTIONAL_TYPE(StructType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of members in the aggregate.
    unsigned n_members() const {return get()->n_parameters();}
    /// \brief Get the type of the given member.
    Term* member_type(std::size_t i) const {return get()->parameter(i);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructType)

    PSI_TVM_FUNCTIONAL_TYPE(StructValue)
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
    PSI_TVM_FUNCTIONAL_TYPE(StructElement)
    typedef unsigned Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the struct being accessed
    Term *aggregate() const {return get()->parameter(0);}
    /// \brief Get the index of the member being accessed
    unsigned index() const {return data();}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *structure, unsigned index);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructElement)

    PSI_TVM_FUNCTIONAL_TYPE(UnionType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of members in the aggregate.
    unsigned n_members() const {return get()->n_parameters();}
    /// \brief Get the type of the given member.
    Term* member_type(std::size_t i) const {return get()->parameter(i);}
    int index_of_type(Term *type) const;
    bool contains_type(Term *type) const;
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionType)

    /// \brief Get the value of a union member
    PSI_TVM_FUNCTIONAL_TYPE(UnionElement)
    typedef unsigned Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the union being accessed
    Term *aggregate() const {return get()->parameter(0);}
    /// \brief Get the type of the member being accessed
    Term *member_type() const {return get()->parameter(1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *aggregate, Term *member_type);
    static Ptr get(Term *aggregate, unsigned index);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionElement)

    PSI_TVM_FUNCTIONAL_TYPE(UnionValue)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of whichever element this represents.
    Term* value() const {return get()->parameter(1);}
    /// \brief Get the type of this value (overloaded to return a UnionType).
    UnionType::Ptr type() const {return cast<UnionType>(TermPtrBase::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(UnionType::Ptr type, Term *value);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionValue)

    PSI_TVM_FUNCTIONAL_TYPE(FunctionSpecialize)
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
