#ifndef HPP_PSI_TVM_FUNCTIONALBUILDER
#define HPP_PSI_TVM_FUNCTIONALBUILDER

#include "../Utility.hpp"

#include "Core.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Utility functions for creating functional operations.
     * 
     * Use this rather than <tt>Op::get</tt> methods since they are much
     * easier to update if the underlying mechanism changes.
     * 
     * This also performs some simple constant folding where possible, in
     * a platform independent way.
     * 
     * Note that when implementing such constant folding operations, the
     * regular operation must still be created in order to perform type
     * checking. This is far from ideal.
     * 
     * \see InstructionBuilder: the corresponding class for instruction terms.
     */
    struct FunctionalBuilder : NonConstructible {
      /// \name Metatype operations
      //@{
      static ValuePtr<> type_type(Context& context, const SourceLocation& location);
      static ValuePtr<> type_value(const ValuePtr<>& size, const ValuePtr<>& alignment, const SourceLocation& location);
      static ValuePtr<> type_size(const ValuePtr<>& type, const SourceLocation& location);
      static ValuePtr<> type_alignment(const ValuePtr<>& type, const SourceLocation& location);
      //@}
      
      /// \name Simple types
      //@{
      static ValuePtr<> block_type(Context& context, const SourceLocation& location);
      static ValuePtr<> empty_type(Context& context, const SourceLocation& location);
      static ValuePtr<> empty_value(Context& context, const SourceLocation& location);
      static ValuePtr<> byte_type(Context& context, const SourceLocation& location);
      static ValuePtr<> byte_pointer_type(Context& context, const SourceLocation& location);
      static ValuePtr<> undef(const ValuePtr<>& type, const SourceLocation& location);
      static ValuePtr<> zero(const ValuePtr<>& type, const SourceLocation& location);
      //@}
      
      /// \name Function types
      //@{
      static ValuePtr<FunctionType> function_type(CallingConvention calling_convention, const ValuePtr<>& result_type,
                                                  const std::vector<ValuePtr<> >& parameter_types, unsigned n_phantom, bool sret, const SourceLocation& location);
      static ValuePtr<FunctionType> constructor_type(Context& context, const SourceLocation& location);
      static ValuePtr<> parameter(const ValuePtr<>& type, unsigned depth, unsigned index, const SourceLocation& location);
      //@}
      
      /// \name Aggregate types
      //@{
      static ValuePtr<> pointer_type(const ValuePtr<>& target_type, const SourceLocation& location);
      static ValuePtr<> pointer_type(const ValuePtr<>& target_type, const ValuePtr<>& upref, const SourceLocation& location);
      static ValuePtr<> upref_type(Context& context, const SourceLocation& location);
      static ValuePtr<> upref(const ValuePtr<>& outer_type, const ValuePtr<>& index, const ValuePtr<>& next, const SourceLocation& location);
      static ValuePtr<> const_type(const ValuePtr<>& value, const SourceLocation& location);
      static ValuePtr<> array_type(const ValuePtr<>& element_type, const ValuePtr<>& length, const SourceLocation& location);
      static ValuePtr<> array_type(const ValuePtr<>& element_type, unsigned length, const SourceLocation& location);
      static ValuePtr<> struct_type(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);
      static ValuePtr<> union_type(Context& context, const std::vector<ValuePtr<> >& members, const SourceLocation& location);
      //@}
      
      /// \name Aggregate values
      //@{
      static ValuePtr<> array_value(const ValuePtr<>&,const std::vector<ValuePtr<> >&, const SourceLocation& location);
      static ValuePtr<> struct_value(Context&,const std::vector<ValuePtr<> >&, const SourceLocation& location);
      static ValuePtr<> union_value(const ValuePtr<>&,const ValuePtr<>&, const SourceLocation& location);
      //@}
      
      /// \name Aggregate element access
      //@{
      static ValuePtr<> element_value(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> element_value(const ValuePtr<>&, unsigned, const SourceLocation& location);
      //@}
      
      /// \name Aggregate pointer functions
      //@{
      static ValuePtr<> outer_ptr(const ValuePtr<>& base, const SourceLocation& location);
      static ValuePtr<> element_ptr(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> element_ptr(const ValuePtr<>&, unsigned, const SourceLocation& location);
      static ValuePtr<> struct_element_offset(const ValuePtr<>&, unsigned, const SourceLocation& location);
      //@}
      
      /// \name Pointer operations
      //@{
      static ValuePtr<> pointer_cast(const ValuePtr<>&, const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> pointer_cast(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> pointer_offset(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> pointer_offset(const ValuePtr<>&, unsigned, const SourceLocation& location);
      //@}
      
      /// \name Integer type and value construction operations
      //@{
      static ValuePtr<> bool_type(Context&, const SourceLocation& location);
      static ValuePtr<> bool_value(Context&, bool, const SourceLocation& location);
      static ValuePtr<IntegerType> int_type(Context&, IntegerType::Width, bool, const SourceLocation& location);
      static ValuePtr<IntegerType> size_type(Context&, const SourceLocation& location);
      static ValuePtr<> int_value(Context&, IntegerType::Width, bool, int, const SourceLocation& location);
      static ValuePtr<> int_value(Context&, IntegerType::Width, bool, unsigned, const SourceLocation& location);
      static ValuePtr<> int_value(Context&, IntegerType::Width, bool, const std::string&, const SourceLocation& location);
      static ValuePtr<> int_value(Context&, IntegerType::Width, bool, const std::string&, bool, const SourceLocation& location);
      static ValuePtr<> int_value(Context&, IntegerType::Width, bool, const std::string&, bool/*=false*/, unsigned/*=10*/, const SourceLocation& location);
      static ValuePtr<> int_value(Context&, IntegerType::Width, bool, const BigInteger&, const SourceLocation& location);
      static ValuePtr<> int_value(const ValuePtr<IntegerType>&, int, const SourceLocation& location);
      static ValuePtr<> int_value(const ValuePtr<IntegerType>&, unsigned, const SourceLocation& location);
      static ValuePtr<> int_value(const ValuePtr<IntegerType>&, const std::string&, const SourceLocation& location);
      static ValuePtr<> int_value(const ValuePtr<IntegerType>&, const std::string&, bool, const SourceLocation& location);
      static ValuePtr<> int_value(const ValuePtr<IntegerType>&, const std::string&, bool/*=false*/, unsigned/*=10*/, const SourceLocation& location);
      static ValuePtr<> int_value(const ValuePtr<IntegerType>&, const BigInteger&, const SourceLocation& location);
      static ValuePtr<> size_value(Context&, unsigned, const SourceLocation& location);
      //@}
      
      /// \name Integer arithmetic operations
      //@{      
      static ValuePtr<> add(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      static ValuePtr<> sub(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      static ValuePtr<> mul(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      static ValuePtr<> div(const ValuePtr<>& lhs, const ValuePtr<>& rhs, const SourceLocation& location);
      static ValuePtr<> neg(const ValuePtr<>& value, const SourceLocation& location);
      //@}
      
      /// \name Integer bitwise operations
      //@{
      static ValuePtr<> bit_and(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> bit_or(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> bit_xor(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> bit_not(const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> bit_shl(const ValuePtr<>& value, const ValuePtr<>& bits_left, const SourceLocation& location);
      static ValuePtr<> bit_shr(const ValuePtr<>& value, const ValuePtr<>& bits_right, const SourceLocation& location);
      static ValuePtr<> bit_shift(const ValuePtr<>& value, int bits_left, const SourceLocation& location);
      //@}
      
      /// \name Integer comparison operations
      //@{      
      static ValuePtr<> cmp_eq(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> cmp_ne(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> cmp_gt(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> cmp_ge(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> cmp_lt(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> cmp_le(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      //@}
      
      /// \name Other integer operations
      //@{
      static ValuePtr<> max(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> min(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> align_to(const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      //@}
      
      static ValuePtr<> float_type(Context&, FloatType::Width, const SourceLocation& location);
      
      static ValuePtr<> bit_cast(const ValuePtr<>& value, const ValuePtr<>& type, const SourceLocation& location);
      static ValuePtr<> select(const ValuePtr<>&, const ValuePtr<>&, const ValuePtr<>&, const SourceLocation& location);
      static ValuePtr<> specialize(const ValuePtr<>&, const std::vector<ValuePtr<> >&, const SourceLocation& location);
      
      static ValuePtr<> apply(const ValuePtr<>&, const std::vector<ValuePtr<> >&, const SourceLocation& location);
      static ValuePtr<> unwrap(const ValuePtr<>& value, const SourceLocation& location);
      static ValuePtr<> unwrap_param(const ValuePtr<>& value, unsigned index, const SourceLocation& location);
    };
  }
}

#endif
