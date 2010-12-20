#ifndef PSI_TVM_TYPE
#define PSI_TVM_TYPE

#include "Functional.hpp"
#include "BigInteger.hpp"

/**
 * \file
 *
 * Contains definitions for core functional types excluding numeric
 * and vector types, i.e. empty types, blocks, pointers, and
 * aggregates.
 */

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(Metatype)
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(EmptyType)
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(EmptyValue)
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(BlockType)

    PSI_TVM_FUNCTIONAL_TYPE(PointerType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the type being pointed to.
    Term* target_type() const {return get()->parameter(0);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *target_type);
    PSI_TVM_FUNCTIONAL_TYPE_END(PointerType)

    PSI_TVM_FUNCTIONAL_TYPE(ArrayType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the array element type.
    Term* element_type() const {return get()->parameter(0);}
    /// \brief Get the array length.
    Term* length() const {return get()->parameter(1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *element_type, Term *length);
    static Ptr get(Term *element_type, const BigInteger& length);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayType)

    PSI_TVM_FUNCTIONAL_TYPE(ArrayValue)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the element type in the array.
    Term* element_type() const {return get()->parameter(0);}
    /// \brief Get the length of the array value.
    std::size_t length() const {return get()->n_parameters() - 1;}
    /// \brief Get the value of the specified array element.
    Term* value(std::size_t n) const {return get()->parameter(n+1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Term *element_type, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(ArrayValue)

    PSI_TVM_FUNCTIONAL_TYPE(StructType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of members in the aggregate.
    std::size_t n_members() const {return get()->n_parameters();}
    /// \brief Get the type of the given member.
    Term* member_type(std::size_t i) const {return get()->parameter(i);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructType)

    PSI_TVM_FUNCTIONAL_TYPE(StructValue)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of elements in the struct.
    std::size_t n_members() const {return get()->n_parameters();}
    /// \brief Get the value of the specified member.
    Term* member_value(std::size_t n) const {return get()->parameter(n);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(StructValue)

    PSI_TVM_FUNCTIONAL_TYPE(UnionType)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the number of members in the aggregate.
    std::size_t n_members() const {return get()->n_parameters();}
    /// \brief Get the type of the given member.
    Term* member_type(std::size_t i) const {return get()->parameter(i);}
    int index_of_type(Term *type) const;
    bool contains_type(Term *type) const;
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, ArrayPtr<Term*const> elements);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionType)

    PSI_TVM_FUNCTIONAL_TYPE(UnionValue)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the type of the union this is a value for.
    UnionType::Ptr type() const {return cast<UnionType>(get()->parameter(0));}
    /// \brief Get the value of whichever element this represents.
    Term* value() const {return get()->parameter(1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(UnionType::Ptr type, Term *value);
    PSI_TVM_FUNCTIONAL_TYPE_END(UnionValue)

    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(BooleanType)

    PSI_TVM_FUNCTIONAL_TYPE(ConstantBoolean)
    typedef bool Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of this constant.
    bool value() const {return data();}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, bool);
    PSI_TVM_FUNCTIONAL_TYPE_END(ConstantBoolean)

    PSI_TVM_FUNCTIONAL_TYPE(IntegerType)
    /// \brief Available integer bit widths
    enum Width {
      i8, ///< 8 bits
      i16, ///< 16 bits
      i32, ///< 32 bits
      i64, ///< 64 bits
      /// Same width as a pointer. For platform independence, this
      /// is not considered equal to any of the other bit widths,
      /// even though in practise it will always be the same as one
      /// of them.
      iptr
    };

    struct Data {
      Width width;
      bool is_signed;
    };

    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the width of this integer type.
    Width width() const {return data().width;}
    /// \brief Whether this integer type is signed.
    bool is_signed() const {return data().is_signed;}
    /// \brief Get the number of bits in this integer type
    unsigned n_bits() const;
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()

    static Ptr get(Context&, Width, bool);
    PSI_TVM_FUNCTIONAL_TYPE_END(IntegerType)

    PSI_TVM_FUNCTIONAL_TYPE(IntegerValue)
    typedef BigInteger Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of this constant.
    const BigInteger& value() const {return data();}
    /// \brief Get the type of this term cast to IntegerType::Ptr
    IntegerType::Ptr type() const {return cast<IntegerType>(FunctionalTermPtrBase<ThisType>::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(IntegerType::Ptr, const BigInteger&);
    PSI_TVM_FUNCTIONAL_TYPE_END(IntegerValue)
  }
}

#endif
