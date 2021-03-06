PSI_TVM_C_OP_STR(add, binary, 6, false, "+")
PSI_TVM_C_OP_STR(sub, binary, 6, false, "-")
PSI_TVM_C_OP_STR(mul, binary, 5, false, "*")
PSI_TVM_C_OP_STR(div, binary, 5, false, "/")
PSI_TVM_C_OP_STR(rem, binary, 5, false, "%")

PSI_TVM_C_OP_STR(shl, binary, 7, false, "<<")
PSI_TVM_C_OP_STR(shr, binary, 7, false, ">>")
PSI_TVM_C_OP_STR(and, binary, 10, false, "&")
PSI_TVM_C_OP_STR(or, binary, 12, false, "|")
PSI_TVM_C_OP_STR(xor, binary, 11, false, "^")

PSI_TVM_C_OP_STR(cmp_eq, binary, 9, false, "==")
PSI_TVM_C_OP_STR(cmp_ne, binary, 9, false, "!=")
PSI_TVM_C_OP_STR(cmp_lt, binary, 8, false, "<=")
PSI_TVM_C_OP_STR(cmp_gt, binary, 8, false, ">=")
PSI_TVM_C_OP_STR(cmp_le, binary, 8, false, "<")
PSI_TVM_C_OP_STR(cmp_ge, binary, 8, false, ">")

PSI_TVM_C_OP_STR(assign, binary, 15, true, "=")

PSI_TVM_C_OP_STR(dereference, unary, 3, true, "*")
PSI_TVM_C_OP_STR(address_of, unary, 3, true, "&")
PSI_TVM_C_OP_STR(negate, unary, 3, true, "-")
PSI_TVM_C_OP_STR(not, unary, 3, true, "~")

PSI_TVM_C_OP_STR(member, member, 2, false, ".")
PSI_TVM_C_OP_STR(ptr_member, member, 2, false, "->")

PSI_TVM_C_OP(global_variable, 0, false)
PSI_TVM_C_OP(function, 0, false)
PSI_TVM_C_OP(parameter, 0, false)
PSI_TVM_C_OP(declare, 0, false)
PSI_TVM_C_OP(vardeclare, 0, false)
PSI_TVM_C_OP(call, 2, false)
PSI_TVM_C_OP(subscript, 2, true)
PSI_TVM_C_OP(literal, 0, false)

// These have the same precedence as the cast operator because
// C99 aggregate literals are written using that operator
PSI_TVM_C_OP(struct_value, 3, true)
PSI_TVM_C_OP(array_value, 3, true)
PSI_TVM_C_OP(union_value, 3, true)

PSI_TVM_C_OP(if, 0, false)
PSI_TVM_C_OP(else, 0, false)
PSI_TVM_C_OP(elif, 0, false)
PSI_TVM_C_OP(endif, 0, false)

PSI_TVM_C_OP(load, 0, false)
PSI_TVM_C_OP(cast, 3, true)
PSI_TVM_C_OP(return, 0, false)
PSI_TVM_C_OP(goto, 0, false)
PSI_TVM_C_OP(ternary, 15, true)
PSI_TVM_C_OP(unreachable, 0, false)
PSI_TVM_C_OP(label, 0, false)
PSI_TVM_C_OP(block_begin, 0, false)
PSI_TVM_C_OP(block_end, 0, false)
