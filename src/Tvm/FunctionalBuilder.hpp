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
      ///@{
      static Term* type_type(Context&);
      static Term* type_value(Term *size, Term *alignment);
      static Term* type_size(Term *type);
      static Term* type_alignment(Term *type);
      ///@}
      
      /// \name Simple types
      ///@{
      static Term* block_type(Context&);
      static Term* empty_type(Context&);
      static Term* empty_value(Context&);
      static Term* byte_type(Context&);
      static Term* byte_pointer_type(Context&);
      static Term* undef(Term*);
      ///@}
      
      /// \name Aggregate types
      ///@{
      static Term* pointer_type(Term*);
      static Term* array_type(Term*,Term*);
      static Term* array_type(Term*,unsigned);
      static Term* struct_type(Context&, ArrayPtr<Term*const>);
      static Term* union_type(Context&, ArrayPtr<Term*const>);
      ///@}
      
      /// \name Aggregate values
      ///@{
      static Term* array_value(Term*,ArrayPtr<Term*const>);
      static Term* struct_value(Context&,ArrayPtr<Term*const>);
      static Term* union_value(Term*,Term*);
      ///@}
      
      /// \name Aggregate element access
      ///@{
      static Term* array_element(Term*, Term*);
      static Term* array_element(Term*, unsigned);
      static Term* struct_element(Term*, unsigned);
      static Term* union_element(Term*, Term*);
      static Term* union_element(Term*, unsigned);
      ///@}
      
      /// \name Aggregate pointer functions
      ///@{
      static Term* array_element_ptr(Term*, Term*);
      static Term* array_element_ptr(Term*, unsigned);
      static Term* struct_element_ptr(Term*, unsigned);
      static Term* union_element_ptr(Term*, Term*);
      static Term* union_element_ptr(Term*, unsigned);
      ///@}
      
      static Term* struct_element_offset(Term*, unsigned);
      
      /// \name Pointer operations
      ///@{
      static Term* pointer_cast(Term*, Term*);
      static Term* pointer_offset(Term*, Term*);
      static Term* pointer_offset(Term*, unsigned);
      ///@}
      
      /// \name Integer type and value construction operations
      ///@{
      static Term* bool_type(Context&);
      static Term* bool_value(Context&, bool);
      static Term* int_type(Context&, IntegerType::Width, bool);
      static Term* size_type(Context&);
      static Term* int_value(Context&, IntegerType::Width, bool, int);
      static Term* int_value(Context&, IntegerType::Width, bool, unsigned);
      static Term* int_value(Context&, IntegerType::Width, bool, const std::string&, bool=false, unsigned=10);
      static Term* int_value(Context&, IntegerType::Width, bool, const BigInteger&);
      static Term* int_value(IntegerType::Ptr, int);
      static Term* int_value(IntegerType::Ptr, unsigned);
      static Term* int_value(IntegerType::Ptr, const std::string&, bool=false, unsigned=10);
      static Term* int_value(IntegerType::Ptr, const BigInteger&);
      static Term* size_value(Context&, unsigned);
      ///@}
      
      /// \name Integer arithmetic operations
      ///@{      
      static Term *add(Term*, Term*);
      static Term *sub(Term*, Term*);
      static Term *mul(Term*, Term*);
      static Term *div(Term*, Term*);
      static Term *neg(Term*);
      ///@}
      
      /// \name Integer bitwise operations
      ///@{
      static Term *bit_and(Term*, Term*);
      static Term *bit_or(Term*, Term*);
      static Term *bit_xor(Term*, Term*);
      static Term *bit_not(Term*);
      ///@}
      
      /// \name Integer comparison operations
      ///@{      
      static Term *cmp_eq(Term*, Term*);
      static Term *cmp_ne(Term*, Term*);
      static Term *cmp_gt(Term*, Term*);
      static Term *cmp_ge(Term*, Term*);
      static Term *cmp_lt(Term*, Term*);
      static Term *cmp_le(Term*, Term*);
      ///@}
      
      /// \name Other integer operations
      ///@{
      static Term *max(Term*, Term*);
      static Term *min(Term*, Term*);
      static Term* align_to(Term*, Term*);
      ///@}
      
      static Term* float_type(Context&, FloatType::Width);
      
      static Term *select(Term*, Term*, Term*);
      static Term *specialize(Term*, ArrayPtr<Term*const>);
      
      static Term* catch_type(Context&);
      static Term* catch_(Term*,unsigned);
    };
  }
}

#endif
