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
      ///@}
      
      /// \name Aggregate types
      ///@{
      static Term* pointer_type(Term*);
      static Term* array_type(Term*,Term*);
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
      
      /// \name Pointer operations
      ///@{
      static Term* pointer_cast(Term*, Term*);
      static Term* pointer_offset(Term*, Term*);
      ///@}
      
      /// \name Integer type and value construction operations
      ///@{
      static Term* int_type(Context&, IntegerType::Width, bool);
      static Term* size_type(Context&);
      static Term* int_value(Term*, int);
      static Term* int_value(Term*, unsigned);
      static Term* int_value(Term*, const std::string&, bool=false, unsigned=10);
      static Term* size_value(Context&, unsigned);
      ///@}
      
      /// \name Integer arithmetic operations
      ///@{      
      static Term *add(Term *lhs, Term *rhs);
      static Term *sub(Term *lhs, Term *rhs);
      static Term *mul(Term *lhs, Term *rhs);
      static Term *div(Term *lhs, Term *rhs);
      static Term *neg(Term *parameter);
      ///@}
      
      /// \name Integer bitwise operations
      ///@{
      static Term *bit_and(Term *lhs, Term *rhs);
      static Term *bit_or(Term *lhs, Term *rhs);
      static Term *bit_xor(Term *lhs, Term *rhs);
      static Term *bit_not(Term *parameter);
      ///@}
      
      /// \name Integer comparison operations
      ///@{      
      static Term *cmp_eq(Term *lhs, Term *rhs);
      static Term *cmp_ne(Term *lhs, Term *rhs);
      static Term *cmp_gt(Term *lhs, Term *rhs);
      static Term *cmp_ge(Term *lhs, Term *rhs);
      static Term *cmp_lt(Term *lhs, Term *rhs);
      static Term *cmp_le(Term *lhs, Term *rhs);
      ///@}
      
      /// \name Other integer operations
      ///@{
      static Term *max(Term *lhs, Term *rhs);
      static Term *min(Term *lhs, Term *rhs);
      static Term* align_to(Term *offset, Term *align);
      ///@}
      
      static Term *select(Term *condition, Term *if_true, Term *if_false);
    };
  }
}

#endif
