#ifndef HPP_PSI_TVM_NUMBER
#define HPP_PSI_TVM_NUMBER

#include "Functional.hpp"

/**
 * \file
 *
 * Definitions for numeric types and operations, including BooleanType
 * and BooleanValue.
 */

#define PSI_TVM_FUNCTIONAL_TYPE_BINARY(name,value_type)                 \
  PSI_TVM_FUNCTIONAL_TYPE(name)                                         \
  typedef Empty Data;                                                   \
  PSI_TVM_FUNCTIONAL_PTR_HOOK()                                         \
  Term* lhs() const {return get()->parameter(0);}                       \
  Term* rhs() const {return get()->parameter(1);}                       \
  value_type::Ptr type() const {return cast<value_type>(TermPtrBase::type());} \
  PSI_TVM_FUNCTIONAL_PTR_HOOK_END()                                     \
  static Ptr get(Term *lhs, Term *rhs);                                 \
  PSI_TVM_FUNCTIONAL_TYPE_END(name)

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_TYPE_SIMPLE(BooleanType)

    PSI_TVM_FUNCTIONAL_TYPE(BooleanValue)
    typedef PrimitiveWrapper<bool> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of this constant.
    bool value() const {return data().value();}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(Context&, bool);
    PSI_TVM_FUNCTIONAL_TYPE_END(BooleanValue)

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
      Data(Width width_, bool is_signed_)
        : width(width_), is_signed(is_signed_) {}

      Width width;
      bool is_signed;

      bool operator == (const Data& other) const {
        return (width == other.width) && (is_signed == other.is_signed);
      }

      friend std::size_t hash_value(const Data& self) {
        std::size_t h = 0;
        boost::hash_combine(h, int(self.width));
        boost::hash_combine(h, self.is_signed);
        return h;
      }
    };

    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the width of this integer type.
    Width width() const {return data().width;}
    /// \brief Whether this integer type is signed.
    bool is_signed() const {return data().is_signed;}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()

    static Ptr get(Context&, Width, bool);
    PSI_TVM_FUNCTIONAL_TYPE_END(IntegerType)

    PSI_TVM_FUNCTIONAL_TYPE(IntegerValue)
    typedef PrimitiveWrapper<uint64_t> Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the value of this constant.
    uint64_t value() const {return data().value();}
    /// \brief Get the type of this term cast to IntegerType::Ptr
    IntegerType::Ptr type() const {return cast<IntegerType>(FunctionalTermPtrBase<ThisType>::type());}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    static Ptr get(IntegerType::Ptr, uint64_t);
    PSI_TVM_FUNCTIONAL_TYPE_END(IntegerValue)

    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerAdd, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerSubtract, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerMultiply, IntegerType)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerDivide, IntegerType)
  }
}

#endif
