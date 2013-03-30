#include "../TermOperationMap.hpp"
#include "../Aggregate.hpp"
#include "../Number.hpp"
#include "../Instructions.hpp"

#include "CModule.hpp"

#include <sstream>
#include <fstream>

namespace Psi {
namespace Tvm {
namespace CBackend {
class ValueBuilder {
  typedef boost::unordered_map<ValuePtr<>, CExpression*> ExpressionMapType;
  ExpressionMapType m_expressions;
  CExpressionBuilder m_c_builder;
  
public:
  CType* build_type(const ValuePtr<>& type);
  CExpression* integer_literal(int value);
  CExpression* build(const ValuePtr<>& value, bool force_eval=false);
  CExpression* build_rvalue(const ValuePtr<>& value);
  CExpressionBuilder& c_builder() {return m_c_builder;}
  CCompiler& c_compiler() {return c_builder().module().c_compiler();}
  
  CExpression* builtin_psi_alloca();
  CExpression* builtin_psi_freea();
  CExpression* builtin_memcpy();
  CExpression* builtin_memset();
};

/**
 * Callbacks which translate TVM operations to C.
 * 
 * Note that certain operations are deliberately not implemented, since they should be removed by AggregateLowering.
 * 
 * These are
 * <ul>
 * <li>Any metatype operations since AggregateLowering should translate these to struct operations</li>
 * <li>Any operations which imply aggregates registers, since these should be removed by AggregateLowering</li>
 * </ul>
 */
struct ValueBuilderCallbacks {
  static const unsigned small_array_size = 8;
    
  static CExpression* empty_value_callback(ValueBuilder& builder, const ValuePtr<EmptyValue>& term) {
    CType *ty = builder.build_type(term->type());
    return builder.c_builder().literal(&term->location(), ty, "{}");
  }
  
  static CExpression* boolean_value_callback(ValueBuilder& builder, const ValuePtr<BooleanValue>& term) {
    CType *ty = builder.build_type(term->type());
    return builder.c_builder().literal(&term->location(), ty, term->value() ? "1" : "0");
  }
  
  static CExpression* integer_value_callback(ValueBuilder& builder, const ValuePtr<IntegerValue>& term) {
    const std::size_t buf_size = 64;
    char buf[buf_size];
    std::size_t n = term->value().print(buf, buf_size, term->is_signed(), 10);
    CType *ty = builder.build_type(term->type());
    CExpression *expr = builder.c_builder().literal(&term->location(), ty, builder.c_builder().strdup(buf));
    if (term->is_signed())
      expr = builder.c_builder().unary(&term->location(), ty, c_eval_never, c_op_negate, expr);
    return expr;
  }
  
  static CExpression* float_value_callback(ValueBuilder& builder, const ValuePtr<FloatValue>& term) {
    PSI_NOT_IMPLEMENTED();
  }
  
  static CExpression* array_value_callback(ValueBuilder& builder, const ValuePtr<ArrayValue>& term) {
    CType *ty = builder.build_type(term->type());
    unsigned n = term->length();
    SmallArray<CExpression*, small_array_size> members(n);
    for (unsigned i = 0; i != n; ++i)
      members[i] = builder.build(term->value(i));
    return builder.c_builder().aggregate_value(&term->location(), c_op_array_value, ty, n, members.get());
  }
  
  static CExpression* struct_value_callback(ValueBuilder& builder, const ValuePtr<StructValue>& term) {
    CType *ty = builder.build_type(term->type());
    unsigned n = term->n_members();
    SmallArray<CExpression*, small_array_size> members(n);
    for (unsigned i = 0; i != n; ++i)
      members[i] = builder.build(term->member_value(i));
    return builder.c_builder().aggregate_value(&term->location(), c_op_struct_value, ty, n, members.get());
  }
  
  static CExpression* union_value_callback(ValueBuilder& builder, const ValuePtr<UnionValue>& term) {
    CType *ty = builder.build_type(term->type());
    unsigned index = term->union_type()->index_of_type(term->value()->type());
    if ((index > 0) && !builder.c_compiler().has_designated_initializer) {
      throw TvmInternalError("C backend error: target compiler does not support designated initializers,"
      " and hence cannot initialize any union member except the first");
    }
    CExpression *member = builder.build(term->value());
    return builder.c_builder().union_value(&term->location(), ty, index, member);
  }
  
  static CExpression* undefined_zero_value_callback(ValueBuilder& builder, const ValuePtr<>& term) {
    CType *ty = builder.build_type(term->type());
    return builder.c_builder().literal(&term->location(), ty, "{0}");
  }
  
  static CExpression* pointer_cast_callback(ValueBuilder& builder, const ValuePtr<PointerCast>& term) {
    CType *ty = builder.build_type(term->type());
    CExpression *val = builder.build(term->pointer());
    return builder.c_builder().cast(&term->location(), ty, val);
  }
  
  static CExpression* pointer_offset_callback(ValueBuilder& builder, const ValuePtr<PointerOffset>& term) {
    CType *ty = builder.build_type(term->type());
    CExpression *ptr = builder.build_rvalue(term->pointer()), *off = builder.build(term->offset());
    return builder.c_builder().binary(&term->location(), ty, c_eval_pure, c_op_add, ptr, off);
  }
  
  static CExpression* element_ptr_callback(ValueBuilder& builder, const ValuePtr<ElementPtr>& term) {
    CExpression *inner = builder.build(term->aggregate_ptr());
    if (isa<StructType>(term) || isa<UnionType>(term)) {
      unsigned idx = size_to_unsigned(term->index());
      return builder.c_builder().member(&term->location(), inner->lvalue ? c_op_member : c_op_ptr_member, inner, idx);
    } else {
      PSI_ASSERT(!inner->lvalue);
      CType *ty = builder.build_type(term->type());
      CExpression *idx = builder.build(term->index());
      return builder.c_builder().binary(&term->location(), ty, c_eval_never, c_op_subscript, inner, idx, true);
    }
  }
  
  static CExpression* select_value_callback(ValueBuilder& builder, const ValuePtr<Select>& term) {
    CType *ty = builder.build_type(term->type());
    CExpression *which = builder.build(term->condition());
    CExpression *if_true = builder.build(term->true_value(), true);
    CExpression *if_false = builder.build(term->false_value(), true);
    return builder.c_builder().ternary(&term->location(), ty, c_eval_pure, c_op_ternary, which, if_true, if_false);
  }

  static CExpression* bitcast_callback(ValueBuilder& builder, const ValuePtr<BitCast>& term) {
    CType *ty = builder.build_type(term->type());
    CExpression *val = builder.build(term->value());
    return builder.c_builder().cast(&term->location(), ty, val);
  }
  
  struct UnaryOpHandler {
    COperatorType op;
    UnaryOpHandler(COperatorType op_) : op(op_) {}
    CExpression* operator () (ValueBuilder& builder, const ValuePtr<UnaryOp>& term) {
      CType *ty = builder.build_type(term->type());
      CExpression *arg = builder.build(term->parameter());
      return builder.c_builder().unary(&term->location(), ty, c_eval_pure, op, arg);
    }
  };
  
  struct BinaryOpHandler {
    COperatorType op;
    BinaryOpHandler(COperatorType op_) : op(op_) {}
    CExpression* operator () (ValueBuilder& builder, const ValuePtr<BinaryOp>& term) {
      CType *ty = builder.build_type(term->type());
      CExpression *lhs = builder.build(term->lhs());
      CExpression *rhs = builder.build(term->rhs());
      return builder.c_builder().binary(&term->location(), ty, c_eval_pure, op, lhs, rhs);
    }
  };
  
  static CExpression* return_callback(ValueBuilder& builder, const ValuePtr<Return>& term) {
    CExpression *value = builder.build(term->value);
    builder.c_builder().unary(&term->location(), NULL, c_eval_write, c_op_return, value);
    return NULL;
  }
  
  static CExpression* conditional_branch_callback(ValueBuilder& builder, const ValuePtr<ConditionalBranch>& term) {
    CExpression *cond = builder.build(term->condition);
    // Use c_eval_never to prevent emitting the statement since we want it inside the if()
    CExpression *if_true = builder.c_builder().unary(&term->location(), NULL, c_eval_never, c_op_goto, builder.build(term->true_target));
    CExpression *if_false = builder.c_builder().unary(&term->location(), NULL, c_eval_never, c_op_goto, builder.build(term->false_target));
    builder.c_builder().ternary(&term->location(), NULL, c_eval_write, c_op_if, cond, if_true, if_false);
    return NULL;
  }

  static CExpression* unconditional_branch_callback(ValueBuilder& builder, const ValuePtr<UnconditionalBranch>& term) {
    builder.c_builder().unary(&term->location(), NULL, c_eval_write, c_op_goto, builder.build(term->target));
    return NULL;
  }
  
  static CExpression* unreachable_callback(ValueBuilder& builder, const ValuePtr<Unreachable>& term) {
    builder.c_builder().nullary(&term->location(), c_op_unreachable);
    return NULL;
  }
  
  static CExpression* function_call_callback(ValueBuilder& builder, const ValuePtr<Call>& term) {
    CExpression *target = builder.build(term->target);
    SmallArray<CExpression*, small_array_size> args;
    args.resize(term->parameters.size());
    
    unsigned sret = 0;
    if (term->target_function_type()->sret()) {
      args[0] = builder.build(term->parameters.back());
      sret = 1;
    }
    
    for (unsigned ii = 0, ie = args.size()-sret; ii != ie; ++ii)
      args[ii+sret] = builder.build(term->parameters[ii]);
    return builder.c_builder().call(&term->location(), target, args.size(), args.get());
  }
  
  static CExpression* load_callback(ValueBuilder& builder, const ValuePtr<Load>& term) {
    CExpression *target = builder.build(term->target);
    if (!target->lvalue) {
      CType *ty = builder.build_type(term->type());
      target = builder.c_builder().unary(&term->location(), ty, c_eval_never, c_op_dereference, target);
    }
    return builder.c_builder().unary(&term->location(), target->type, c_eval_read, c_op_load, target);
  }
  
  static CExpression* store_callback(ValueBuilder& builder, const ValuePtr<Store>& term) {
    CExpression *value = builder.build_rvalue(term->value);
    CExpression *target = builder.build(term->target);
    if (!target->lvalue)
      target = builder.c_builder().unary(&term->location(), value->type, c_eval_never, c_op_dereference, target);
    builder.c_builder().binary(&term->location(), NULL, c_eval_write, c_op_assign, target, value);
    return NULL;
  }
  
  static CExpression* alloca_callback(ValueBuilder& builder, const ValuePtr<Alloca>& term) {
    builder.c_builder().nullary(&term->location(), c_op_block_begin);
    
    boost::optional<unsigned> max_count;
    
    CExpression *count = NULL;
    boost::optional<unsigned> known_count;
    if (term->count) {
      count = builder.build(term->count);
      boost::optional<unsigned> known_count;
      if (ValuePtr<IntegerValue> int_val = dyn_cast<IntegerValue>(term->count))
        known_count = int_val->value().unsigned_value();
    } else {
      known_count = 1;
    }
    
    unsigned alignment_value;
    CExpression *alignment_expr;
    if (term->alignment) {
      alignment_expr = builder.build(term->alignment);
      alignment_value = 16;
      if (ValuePtr<IntegerValue> iv = dyn_cast<IntegerValue>(term->alignment)) {
        if (boost::optional<unsigned> alignment_known = iv->value().unsigned_value())
          alignment_value = *alignment_known;
      }
    } else {
      alignment_expr = builder.integer_literal(1);
      alignment_value = 0;
    }
    
    CType *ptr_ty = builder.build_type(term->type());
    CType *el_ty = builder.build_type(term->element_type);

    bool has_vla = builder.c_compiler().has_variable_length_arrays;
    if ((has_vla || known_count) && (!max_count || (known_count && (*known_count <= *max_count)))) {
      if (known_count && (*known_count == 1)) {
        return builder.c_builder().declare(&term->location(), el_ty, c_op_declare, NULL, alignment_value);
      } else {
        return builder.c_builder().declare(&term->location(), el_ty, c_op_vardeclare, count, alignment_value);
      }
    } else if (!has_vla || (max_count && (*max_count == 0))) {
      CExpression *args[2] = {count, alignment_expr};
      CExpression *ptr = builder.c_builder().call(&term->location(), builder.builtin_psi_alloca(), 2, args, true);
      return builder.c_builder().unary(&term->location(), ptr_ty, c_eval_write, c_op_cast, ptr);
    } else {
      PSI_ASSERT(has_vla);
      PSI_ASSERT(count && max_count);
      // Need to check whether we have fewer or more than the maximum number of elements
      CExpression *count_is_large = builder.c_builder().binary(&term->location(), NULL, c_eval_pure, c_op_cmp_ge, count, builder.integer_literal(*max_count));
      CExpression *local_count = builder.c_builder().ternary(&term->location(), NULL, c_eval_write, c_op_ternary, count_is_large, builder.integer_literal(0), count);
      CExpression *count = builder.build(term->count);
      CExpression *local_alloc = builder.c_builder().declare(&term->location(), el_ty, c_op_vardeclare, local_count, alignment_value);
      CExpression *call_args[2] = {count, alignment_expr};
      CExpression *call_alloc = builder.c_builder().call(&term->location(), builder.builtin_psi_alloca(), 2, call_args);
      return builder.c_builder().ternary(&term->location(), ptr_ty, c_eval_pure, c_op_ternary, count_is_large, call_alloc, local_alloc);
    }
  }
  
  static CExpression* alloca_const_callback(ValueBuilder& builder, const ValuePtr<AllocaConst>& term) {
    CExpression *value = builder.build(term->value);
    builder.c_builder().nullary(&term->location(), c_op_block_begin);
    return builder.c_builder().binary(&term->location(), value->type, c_eval_write, c_op_declare, value, NULL);
  }
  
  static CExpression* freea_callback(ValueBuilder& builder, const ValuePtr<FreeAlloca>& term) {
    CExpression *src = builder.build(term->value);
    if (src->op == c_op_ternary) {
      PSI_ASSERT(builder.c_compiler().has_variable_length_arrays);
      CExpressionTernary *src_ternary = checked_cast<CExpressionTernary*>(src);
      CExpressionCall *base_call = checked_cast<CExpressionCall*>(src_ternary->second);
      CExpression *call_args[3] = {src, base_call->args[0], base_call->args[1]};
      CExpression *free_op = builder.c_builder().call(&term->location(), builder.builtin_psi_freea(), 3, call_args, true);
      builder.c_builder().ternary(&term->location(), NULL, c_eval_write, c_op_if, src_ternary->first, free_op, NULL);
    } else if (src->op == c_op_cast) {
      CExpressionUnary *src_unary = checked_cast<CExpressionUnary*>(src);
      CExpressionCall *base_call = checked_cast<CExpressionCall*>(src_unary->arg);
      CExpression *call_args[3] = {src, base_call->args[0], base_call->args[1]};
      builder.c_builder().call(&term->location(), builder.builtin_psi_freea(), 3, call_args);
    }
    builder.c_builder().nullary(&term->location(), c_op_block_end);
    return NULL;
  }
  
  static CExpression* evaluate_callback(ValueBuilder& builder, const ValuePtr<Evaluate>& term) {
    builder.build(term->value, true);
    return NULL;
  }
  
  static CExpression* memcpy_callback(ValueBuilder& builder, const ValuePtr<MemCpy>& term) {
    CExpression *args[3];
    args[0] = builder.build_rvalue(term->dest);
    args[1] = builder.build_rvalue(term->src);
    args[2] = builder.build(term->count);
    builder.c_builder().call(&term->location(), builder.builtin_memcpy(), 3, args);
    return NULL;
  }
  
  static CExpression* memzero_callback(ValueBuilder& builder, const ValuePtr<MemZero>& term) {
    CExpression *args[3];
    args[0] = builder.build_rvalue(term->dest);
    args[1] = builder.integer_literal(0);
    args[2] = builder.build(term->count);
    builder.c_builder().call(&term->location(), builder.builtin_memset(), 3, args);
    return NULL;
  }
    
  static CExpression* default_throw_callback(ValueBuilder&, const ValuePtr<>&) {
    throw TvmInternalError("Term type not supported in TVM to C lowering. See documenation for ValueBuilderCallbacks.");
  }
  
  typedef TermOperationMap<FunctionalValue, CExpression*, ValueBuilder&> FunctionalCallbackMap;
  static FunctionalCallbackMap functional_callback_map;
  static FunctionalCallbackMap::Initializer functional_callback_map_initializer() {
    return FunctionalCallbackMap::initializer(default_throw_callback)
      .add<EmptyValue>(empty_value_callback)
      .add<BooleanValue>(boolean_value_callback)
      .add<IntegerValue>(integer_value_callback)
      .add<FloatValue>(float_value_callback)
      .add<ArrayValue>(array_value_callback)
      .add<StructValue>(struct_value_callback)
      .add<UnionValue>(union_value_callback)
      .add<UndefinedValue>(undefined_zero_value_callback)
      .add<ZeroValue>(undefined_zero_value_callback)
      .add<PointerCast>(pointer_cast_callback)
      .add<PointerOffset>(pointer_offset_callback)
      .add<ElementPtr>(element_ptr_callback)
      .add<Select>(select_value_callback)
      .add<BitCast>(bitcast_callback)
      .add<ShiftLeft>(BinaryOpHandler(c_op_shl))
      .add<ShiftRight>(BinaryOpHandler(c_op_shr))
      .add<IntegerAdd>(BinaryOpHandler(c_op_add))
      .add<IntegerMultiply>(BinaryOpHandler(c_op_mul))
      .add<IntegerDivide>(BinaryOpHandler(c_op_div))
      .add<IntegerNegative>(UnaryOpHandler(c_op_negate))
      .add<BitAnd>(BinaryOpHandler(c_op_and))
      .add<BitOr>(BinaryOpHandler(c_op_or))
      .add<BitXor>(BinaryOpHandler(c_op_xor))
      .add<BitNot>(UnaryOpHandler(c_op_not))
      .add<IntegerCompareEq>(BinaryOpHandler(c_op_cmp_eq))
      .add<IntegerCompareNe>(BinaryOpHandler(c_op_cmp_ne))
      .add<IntegerCompareGt>(BinaryOpHandler(c_op_cmp_gt))
      .add<IntegerCompareLt>(BinaryOpHandler(c_op_cmp_lt))
      .add<IntegerCompareGe>(BinaryOpHandler(c_op_cmp_ge))
      .add<IntegerCompareLe>(BinaryOpHandler(c_op_cmp_gt));
  }
  
  typedef TermOperationMap<Instruction, CExpression*, ValueBuilder&> InstructionCallbackMap;
  static InstructionCallbackMap instruction_callback_map;
  static InstructionCallbackMap::Initializer instruction_callback_map_initializer() {
    return InstructionCallbackMap::initializer(default_throw_callback)
      .add<Return>(return_callback)
      .add<ConditionalBranch>(conditional_branch_callback)
      .add<UnconditionalBranch>(unconditional_branch_callback)
      .add<Unreachable>(unreachable_callback)
      .add<Call>(function_call_callback)
      .add<Load>(load_callback)
      .add<Store>(store_callback)
      .add<Alloca>(alloca_callback)
      .add<AllocaConst>(alloca_const_callback)
      .add<FreeAlloca>(freea_callback)
      .add<Evaluate>(evaluate_callback)
      .add<MemCpy>(memcpy_callback)
      .add<MemZero>(memzero_callback);
  }
};

ValueBuilderCallbacks::FunctionalCallbackMap ValueBuilderCallbacks::functional_callback_map(ValueBuilderCallbacks::functional_callback_map_initializer());
ValueBuilderCallbacks::InstructionCallbackMap ValueBuilderCallbacks::instruction_callback_map(ValueBuilderCallbacks::instruction_callback_map_initializer());

/**
 * \brief Build a value
 * 
 * \param force_eval This is currently not used; since any re-used value will be emitted
 * as a variable where it is first used, as will any value with definite side effects. The
 * only case where this will fail is where a divide-by-zero error does not occur because
 * it appears inside the branch of a select() expression which is not used. In future this
 * might be changed to name all values which have \c force_eval set.
 */
CExpression* ValueBuilder::build(const ValuePtr<>& value, bool PSI_UNUSED(force_eval)) {
  ExpressionMapType::const_iterator it = m_expressions.find(value);
  if (it != m_expressions.end()) {
    PSI_ASSERT(it->second);
    return it->second;
  }
  
  CExpression *expr;
  if (ValuePtr<FunctionalValue> fv = dyn_cast<FunctionalValue>(value))
    expr = ValueBuilderCallbacks::functional_callback_map.call(*this, fv);
  else if (ValuePtr<Instruction> iv = dyn_cast<Instruction>(value))
    expr = ValueBuilderCallbacks::instruction_callback_map.call(*this, iv);
  else
    PSI_FAIL("Unexpected expression type");
  
  if (expr || PSI_DEBUG)
    m_expressions.insert(std::make_pair(value, expr));
  
  return expr;
}

/**
 * \brief Return an expression as a C rvalue.
 * 
 * C has some support for lvalue references; that is
 * \code a.b = c \endcode
 * does actually work, so <tt>a.b</tt> is a reference and <tt>&a.b</tt> is a pointer.
 * The C translation of TVM uses this; sometimes a pointer is in fact a reference, particularly
 * <tt>a->b</tt> is the translation of <tt>element_ptr</tt>, but the result is itself not a
 * pointer. This function forces the result to be a pointer; if building \c value gives an
 * lvalue reference then it is wrapped in a pointer-to operator, that is <tt>&value</tt>.
 */
CExpression* ValueBuilder::build_rvalue(const ValuePtr<>& value) {
  CExpression *inner = build(value);
  if (inner->lvalue) {
    CType *ty = build_type(value->type());
    return c_builder().unary(&value->location(), ty, c_eval_never, c_op_address_of, inner);
  } else {
    return inner;
  }
}

/**
 * \brief Get an integer literal.
 */
CExpression* ValueBuilder::integer_literal(int value) {
  PSI_NOT_IMPLEMENTED();
}
}
}
}
