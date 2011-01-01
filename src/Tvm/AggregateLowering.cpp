#include "Aggregate.hpp"
#include "FunctionalRewrite.hpp"
#include "Instructions.hpp"
#include "Number.hpp"

using namespace Psi;
using namespace Psi::Tvm;

namespace {
  /**
   * Interface for lowering function calls and function types by
   * removing aggregates from the IR. This is necessarily target
   * specific, so must be done by callbacks.
   */
  struct AggregateFunctionLowering {
    /**
     * Cast the type of function being called and adjust the
     * parameters to remove aggregates.
     */
    virtual void lower_function_call() = 0;

    /**
     * Change a function's type and add entry code to decode
     * parameters into aggregates in the simplest possible way. The
     * remaining aggregates lowering code will handle the rest.
     */
    virtual void lower_function() = 0;
  };

  struct AggregateLoweringData {
    /// Whether to replace all unions in the IR with pointer operations
    bool remove_all_unions;
    /**
     * Whether to only rewrite aggregate operations which act on types
     * whose binary representation is not fully known. \c
     * remove_all_unions affects the behaviour of this option, since
     * if \c remove_all_unions is true \em any type containing a union
     * is considered not fully known.
     *
     * Note that operations to compute the size and alignment of types
     * are always completely rewritten, regardless of this setting.
     */
    bool remove_only_unknown;
    /**
     * Target specific function call and entry lowering code.
     */
    AggregateFunctionLowering *function_lowering;
  };

  typedef FunctionalTermRewriter<AggregateLoweringData> AggregateLoweringRewriter;

  /**
   * Align an offset to a specified alignment, which must be a
   * power of two.
   *
   * The formula used is: <tt>(offset + align - 1) & ~(align - 1)</tt>
   */
  Term* align_to(Term *offset, Term *align) {
    Context& context = offset->context();
    Term *one = IntegerValue::get(IntegerType::get_size(context), 1);
    Term *align_minus_one = IntegerSubtract::get(align, one);
    Term *offset_plus_align_minus_one = IntegerAdd::get(offset, align_minus_one);
    Term *not_align_minus_one = BitNot::get(align_minus_one);
    return BitAnd::get(offset_plus_align_minus_one, not_align_minus_one);
  }

  /**
   * Get the maximum of two terms, assuming they are integers.
   */
  Term* integer_term_max(Term *lhs, Term *rhs) {
    Term *cmp = IntegerCompare::get(cmp_gt, lhs, rhs);
    return SelectValue::get(cmp, lhs, rhs);
  }

  Term *array_type_rewrite(AggregateLoweringRewriter&, ArrayType::Ptr term) {
    Term *element_type = term->element_type();
    return MetatypeValue::get(IntegerMultiply::get(MetatypeSize::get(element_type), term->length()),
			      MetatypeAlignment::get(element_type));
  }

  Term* struct_type_rewrite(AggregateLoweringRewriter& rewriter, StructType::Ptr term) {
    IntegerType::Ptr intptr_type = IntegerType::get_size(rewriter.context());
    Term *size = IntegerValue::get(intptr_type, 0);
    Term *alignment = IntegerValue::get(intptr_type, 1);

    for (unsigned i = 0, e = term->n_members(); i != e; ++i) {
      Term *member_type = term->member_type(i);
      Term *member_size = MetatypeSize::get(member_type);
      Term *member_alignment = MetatypeSize::get(member_type);
      size = IntegerAdd::get(align_to(size, member_alignment), member_size);
      alignment = integer_term_max(alignment, member_alignment);
    }

    size = align_to(size, alignment);
    return MetatypeValue::get(size, alignment);
  }

  Term* union_type_rewrite(AggregateLoweringRewriter& rewriter, UnionType::Ptr term) {
    IntegerType::Ptr intptr_type = IntegerType::get_size(rewriter.context());
    Term *size = IntegerValue::get(intptr_type, 0);
    Term *alignment = IntegerValue::get(intptr_type, 1);

    for (unsigned i = 0, e = term->n_members(); i != e; ++i) {
      Term *member_type = term->member_type(i);
      Term *member_size = MetatypeSize::get(member_type);
      Term *member_alignment = MetatypeSize::get(member_type);
      size = integer_term_max(size, member_size);
      alignment = integer_term_max(alignment, member_alignment);
    }

    size = align_to(size, alignment);
    return MetatypeValue::get(size, alignment);
  }

  AggregateLoweringRewriter::CallbackMap aggregate_lowering_callbacks =
    AggregateLoweringRewriter::callback_map_initializer()
    .add<ArrayType>(array_type_rewrite)
    .add<StructType>(struct_type_rewrite)
    .add<UnionType>(union_type_rewrite);
}

namespace Psi {
  namespace Tvm {
  }
}
