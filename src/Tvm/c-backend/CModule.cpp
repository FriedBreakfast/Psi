#include "CModule.hpp"

#include <algorithm>
#include <cstdio>

namespace Psi {
namespace Tvm {
namespace CBackend {
/**
 * C operators we use.
 * 
 * The precedence is taken from <a href="http://en.cppreference.com/w/cpp/language/operator_precedence">cppreference.com</a>.
 */
const COperator c_operators[] = {
#define PSI_TVM_C_OP(op,prec,right_assoc) {c_expr_##op, prec, right_assoc, NULL},
#define PSI_TVM_C_OP_STR(op,ty,prec,right_assoc,str) {c_expr_##ty, prec, right_assoc, str},
#include "COperators.def"
#undef PSI_TVM_C_OP_STR
#undef PSI_TVM_C_OP
};

void CExpressionBuilder::append(const SourceLocation* location, CExpression *expr) {
  expr->name.prefix = NULL;
  expr->location = location;
  expr->requires_name = false;
  if (m_block)
    m_block->instructions.append(expr);
}

const char* CExpressionBuilder::strdup(const char *s) {
  return m_module->pool().strdup(s);
}

CExpression* CExpressionBuilder::unary(const SourceLocation* location, CType *ty, CExpressionEvaluation eval, COperatorType op, CExpression *arg, bool lvalue) {
  CExpressionUnary *unary = m_module->pool().alloc<CExpressionUnary>();
  unary->type = ty;
  unary->op = op;
  unary->arg = arg;
  unary->eval = eval;
  unary->lvalue = lvalue;
  append(location, unary);
  return unary;
}

CExpression* CExpressionBuilder::binary(const SourceLocation* location, CType *ty, CExpressionEvaluation eval, COperatorType op, CExpression *left, CExpression *right, bool lvalue) {
  CExpressionBinary *bin = m_module->pool().alloc<CExpressionBinary>();
  bin->type = ty;
  bin->op = op;
  bin->left = left;
  bin->right = right;
  bin->eval = eval;
  bin->lvalue = lvalue;
  append(location, bin);
  return bin;
}

CExpression* CExpressionBuilder::ternary(const SourceLocation* location, CType *ty, CExpressionEvaluation eval, COperatorType op, CExpression *a, CExpression *b, CExpression *c) {
  CExpressionTernary *bin = m_module->pool().alloc<CExpressionTernary>();
  bin->type = ty;
  bin->op = op;
  bin->first = a;
  bin->second = b;
  bin->third = c;
  bin->eval = eval;
  bin->lvalue = false;
  append(location, bin);
  return bin;
}

CExpression* CExpressionBuilder::member(const SourceLocation* location, COperatorType op, CExpression *arg, unsigned index) {
  CExpressionBinaryIndex *sub = m_module->pool().alloc<CExpressionBinaryIndex>();
  if (op == c_op_member) {
    CTypeAggregate *agg = checked_cast<CTypeAggregate*>(arg->type);
    PSI_ASSERT(index < agg->n_members);
    sub->type = agg->members[index].type;
    sub->op = c_op_member;
  } else {
    CTypeAggregate *agg = checked_cast<CTypeAggregate*>(checked_cast<CTypePointer*>(arg->type)->target);
    PSI_ASSERT(index < agg->n_members);
    sub->type = agg->members[index].type;
    sub->op = c_op_ptr_member;
  }
  sub->arg = arg;
  sub->index = index;
  sub->eval = c_eval_never;
  if (sub->type->type == c_type_array) {
    sub->type = pointer_type(location, checked_cast<CTypeArray*>(sub->type)->member);
    sub->lvalue = false;
  } else {    
    sub->lvalue = true;
  }
  append(location, sub);
  return sub;
}

CExpression* CExpressionBuilder::declare(const SourceLocation* location, CType *type, COperatorType op, CExpression *arg, unsigned index) {
  CExpressionBinaryIndex *sub = m_module->pool().alloc<CExpressionBinaryIndex>();
  if (op == c_op_vardeclare) {
    sub->type = pointer_type(location, type);
    sub->lvalue = false;
    sub->op = c_op_vardeclare;
  } else {
    sub->lvalue = true;
    sub->type = type;
    sub->op = c_op_declare;
  }
  sub->arg = arg;
  sub->index = index;
  sub->eval = c_eval_write;
  append(location, sub);
  return sub;
}

CExpression* CExpressionBuilder::literal(const SourceLocation* location, CType *ty, const char *str) {
  CExpressionLiteral *lit = m_module->pool().alloc<CExpressionLiteral>();
  lit->type = ty;
  lit->op = c_op_literal;
  lit->str = str;
  lit->eval = c_eval_never;
  lit->lvalue = false;
  append(location, lit);
  return lit;
}

CExpression* CExpressionBuilder::call(const SourceLocation* location, CExpression *target, unsigned n_args, CExpression *const* args, bool conditional) {
  CExpressionCall *call = (CExpressionCall*)m_module->pool().alloc(sizeof(CExpressionCall) + n_args*sizeof(CExpression*), PSI_ALIGNOF(CExpressionCall));
  call->type = checked_cast<CTypeFunction*>(target->type)->result_type;
  call->op = c_op_call;
  call->target = target;
  call->n_args = n_args;
  call->eval = (conditional ? c_eval_write : c_eval_never);
  call->lvalue = false;
  std::copy(args, args+n_args, call->args);
  append(location, call);
  return call;
}

CExpression* CExpressionBuilder::aggregate_value(const SourceLocation* location, COperatorType op, CType *ty, unsigned n_members, CExpression *const* members) {
  PSI_ASSERT(n_members == ((op == c_op_array_value) ? checked_cast<CTypeArray*>(ty)->length : checked_cast<CTypeAggregate*>(ty)->n_members));
  CExpressionAggregateValue *agg = (CExpressionAggregateValue*)m_module->pool().alloc(sizeof(CExpressionAggregateValue) + n_members*sizeof(CExpression*), PSI_ALIGNOF(CExpressionAggregateValue));
  agg->type = ty;
  agg->op = op;
  agg->n_members = n_members;
  agg->eval = c_eval_read;
  std::copy(members, members+n_members, agg->members);
  append(location, agg);
  return agg;
}

CExpression* CExpressionBuilder::union_value(const SourceLocation* location, CType *ty, unsigned index, CExpression *value) {
  PSI_ASSERT(index < checked_cast<CTypeAggregate*>(ty)->n_members);
  CExpressionUnionValue *agg = m_module->pool().alloc<CExpressionUnionValue>();
  agg->type = ty;
  agg->op = c_op_union_value;
  agg->index = index;
  agg->value = value;
  agg->eval = c_eval_read;
  append(location, agg);
  return agg;
}

CExpression* CExpressionBuilder::cast(const SourceLocation* location, CType *ty, CExpression *arg) {
  CExpressionCast *cast = m_module->pool().alloc<CExpressionCast>();
  cast->type = ty;
  cast->op = c_op_cast;
  cast->arg = arg;
  cast->eval = c_eval_pure;
  append(location, cast);
  return cast;
}

void CExpressionBuilder::nullary(const SourceLocation* location, COperatorType op) {
  CExpression *expr = m_module->pool().alloc<CExpression>();
  expr->type = NULL;
  expr->eval = c_eval_write;
  expr->op = op;
  append(location, expr);
}

CType* CExpressionBuilder::pointer_type(const SourceLocation* location, CType *target) {
  if (target->ptr)
    return target->ptr;
  
  CTypePointer *tu = m_module->pool().alloc<CTypePointer>();  
  tu->type = c_type_pointer;
  tu->name.prefix = NULL;
  tu->target = target;
  tu->location = location;
  target->ptr = tu;
  return tu;
}

CType* CExpressionBuilder::function_type(const SourceLocation* location, CType *result_ty, unsigned n_args, const CTypeFunctionArgument *args) {
  CTypeFunction *f = (CTypeFunction*)m_module->pool().alloc(sizeof(CTypeFunction) + n_args*sizeof(CTypeFunctionArgument), PSI_ALIGNOF(CTypeFunction));
  f->type = c_type_function;
  f->name.prefix = NULL;
  f->location = location;
  f->ptr = NULL;
  f->result_type = result_ty;
  f->n_args = n_args;
  std::copy(args, args+n_args, f->args);
  return f;
}

CType* CExpressionBuilder::aggregate_type(const SourceLocation* location, CTypeType op, unsigned n_members, const CTypeAggregateMember *members) {
  CTypeAggregate *agg = (CTypeAggregate*)m_module->pool().alloc(sizeof(CTypeAggregate) + n_members*sizeof(CTypeAggregateMember), PSI_ALIGNOF(CTypeAggregate));
  agg->type = op;
  agg->ptr = NULL;
  agg->name.prefix = NULL;
  agg->location = location;
  agg->n_members = n_members;
  std::copy(members, members+n_members, agg->members);
  return agg;
}

CType* CExpressionBuilder::struct_type(const SourceLocation* location, unsigned n_members, const CTypeAggregateMember *members) {
  return aggregate_type(location, c_type_struct, n_members, members);
}

CType* CExpressionBuilder::union_type(const SourceLocation* location, unsigned n_members, const CTypeAggregateMember *members) {
  return aggregate_type(location, c_type_union, n_members, members);
}

std::ostream& operator << (std::ostream& os, const CName& name) {
  os << name.prefix;
  if (name.index > 0)
    os << name.index;
  return os;
}

CModuleEmitter::CModuleEmitter(std::ostream *output, CModule *module)
: m_module(module), m_output(output), m_file(NULL) {
}

/**
 * Print part of a type which precedes its name.
 */
void CModuleEmitter::emit_type_prolog(CType *type) {
  if (type->name.prefix) {
    output() << type->name << ' ';
  } else {
    switch (type->type) {
    case c_type_array: {
      CTypeArray *arr = checked_cast<CTypeArray*>(type);
      emit_type_prolog(arr->member);
      break;
    }
    
    case c_type_pointer: {
      unsigned count = 0;
      CType *inner = type;
      while (inner->type == c_type_pointer) {
        CTypePointer *ptr = checked_cast<CTypePointer*>(inner);
        ++count;
        inner = ptr->target;
      }
      PSI_ASSERT(inner->name.prefix);
      output() << inner->name << ' ';
      for (; count; --count) output() << '*';
      break;
    }
    
    case c_type_function: {
      CTypeFunction *ftype = checked_cast<CTypeFunction*>(type);
      emit_type_prolog(ftype->result_type);
      break;
    }
      
    default: PSI_FAIL("C type should be named; it cannot be printed directly");
    }
  }
}

/**
 * Print part of a type which follows its name.
 */
void CModuleEmitter::emit_type_epilog(CType *type) {
  if (!type->name.prefix) {
    switch (type->type) {
    case c_type_array: {
      CTypeArray *arr = checked_cast<CTypeArray*>(type);
      output() << '[' << arr->length << ']';
      break;
    }
    
    case c_type_pointer:
      break;
    
    case c_type_function: {
      CTypeFunction *ftype = checked_cast<CTypeFunction*>(type);
      output() << '(';
      for (unsigned ii = 0, ie = ftype->n_args; ii != ie; ++ii) {
        if (ii) output() << ", ";
        CTypeFunctionArgument& arg = ftype->args[ii];
        emit_type_prolog(arg.type);
        emit_type_epilog(arg.type);
      }
      output() << ')';
      break;
    }
      
    default: PSI_FAIL("C type should be named; it cannot be printed directly");
    }
  }
}

/**
 * \brief Print a line number, and filename if the file has changed.
 */
void CModuleEmitter::emit_location(const SourceLocation& location) {
  output() << "#line " << location.physical.first_line;
  if (m_file != location.physical.file.get()) {
    m_file = location.physical.file.get();
    output() << " \"";
    emit_string(m_file->url.c_str());
    output() << '\"';
  }
  output() << '\n';
}

/**
 * \brief Print a string with escapes.
 */
void CModuleEmitter::emit_string(const char *s) {
  const char *escapes = "\aa\bb\ff\nn\rr\tt\vv\'\'\"\"\\\\";
  
  const std::locale& c_locale = std::locale::classic();
  
  for (const char *p = s; *p; ++p) {
    unsigned char c = *p;
    
    if ((c < 128) && std::isgraph(c, c_locale)) {
      output().put(c);
    } else if (c == ' ') {
      output().put(' ');
    } else if (c == '?') {
      // Avoid trigraph interpretation
      if ((p != s) && (p[-1] == '?'))
        output().put('\\');
      output().put('?');
    } else {
      // Check for a standard escape character
      if (const char *escapes_p = std::strchr(escapes, c)) {
        output().put('\\').put(escapes_p[1]);
      } else {
        // Print octal code
        char data[5];
        // If the next character is a digit, force a 3 digit octal escape to
        // avoid the next character being interpreted as part of the escape sequence
        unsigned width = std::isdigit(p[1], c_locale) ? 3 : 1;
        unsigned count = std::sprintf(data, "\\%*o", width, c);
        PSI_ASSERT(count < 5);
        output().write(data, count);
      }      
    }
  }
}

/// Emit type declarations
void CModuleEmitter::emit_types() {
  for (SinglyLinkedList<CType>::iterator ii = m_module->types().begin(), ie = m_module->types().end(); ii != ie; ++ii) {
    CType& ty = *ii;
    switch (ty.type) {
    case c_type_builtin:
    case c_type_pointer:
    case c_type_array:
      break;
      
    case c_type_function: {
      emit_location(*ty.location);
      output() << "typedef ";
      emit_type_prolog(&ty);
      output() << ty.name;
      emit_type_epilog(&ty);
      output() << ";\n";
      break;
    }

    case c_type_union:
    case c_type_struct: {
      emit_location(*ty.location);
      CTypeAggregate& agg = checked_cast<CTypeAggregate&>(ty);
      output() << (ty.type == c_type_union ? "union" : "struct") << " {\n";
      for (unsigned ii = 0, ie = agg.n_members; ii != ie; ++ii) {
        emit_type_prolog(agg.members[ii].type);
        output() << " m" << ii;
        emit_type_epilog(agg.members[ii].type);
        output() << ";\n";
      }
      output() << "}";
      break;
    }
    
    default: PSI_FAIL("Unknown C type category");
    }
  }
}

/**
 * Emit global variable or function declaration.
 * 
 * Note that this does not emit a semicolon and newline after the declaration, which allows
 * emit_definition to re-use this code.
 */
void CModuleEmitter::emit_declaration(CGlobal& global) {
  CGlobalVariable *global_var = NULL;
  output() << (global.is_private ? "static " : "extern ");
  if (global.alignment) c_compiler().emit_alignment(*this, global.alignment);
  if (global.op == c_op_global_variable) {
    global_var = checked_cast<CGlobalVariable*>(&global);
    if (global_var->is_const) output() << "const ";
  }
  
  emit_type_prolog(global.type);
  output() << global.name;
  emit_type_epilog(global.type);
}

/// Emit global variable or function definition
void CModuleEmitter::emit_definition(CGlobal& global) {
  emit_location(*global.location);
  if (global.op == c_op_global_variable) {
    CGlobalVariable& gvar = checked_cast<CGlobalVariable&>(global);
    if (gvar.value) {
      emit_declaration(gvar);
      output() << " = ";
      output() << ";\n";
    }
  } else {
    PSI_ASSERT(global.op == c_op_function);
    CFunction& func = checked_cast<CFunction&>(global);
    if (!func.blocks.empty()) {
      emit_declaration(func);
      output() << "{\n";
      output() << "}\n";
    }
  }
}

/**
 * \brief Emit the definition of an expression.
 * 
 * This will not print the name of the expression.
 */
void CModuleEmitter::emit_expression_def(CExpression *expression, unsigned precedence, bool is_right) {
  COperatorType op_idx = expression->op;
  const COperator& op = c_operators[op_idx];
  
  bool has_brackets = false;
  if (op.precedence > precedence) {
    has_brackets = true;
  } else if (op.precedence == precedence) {
    has_brackets = (is_right != op.right_associative);
  }
  
  if (has_brackets) output().put('(');
  
  switch (op.type) {
  case c_expr_declare: {
    CExpressionBinaryIndex *binary = checked_cast<CExpressionBinaryIndex*>(expression);
    if (binary->index)
      c_compiler().emit_alignment(*this, binary->index);
    emit_type_prolog(binary->type);
    output() << binary->name;
    emit_type_epilog(binary->type);
    if (binary->arg) {
      output() << " = ";
      emit_expression(binary->arg);
    }
    output() << ";\n";
    break;
  }
  
  case c_expr_vardeclare: {
    CExpressionBinaryIndex *binary = checked_cast<CExpressionBinaryIndex*>(expression);
    if (binary->index)
      c_compiler().emit_alignment(*this, binary->index);
    emit_type_prolog(binary->type);
    output() << binary->name;
    emit_type_epilog(binary->type);
    output() << '[';
    emit_expression(binary->arg);
    output() << ']' << ";\n";
    break;
  }
  
  case c_expr_call: {
    CExpressionCall *call = checked_cast<CExpressionCall*>(expression);
    emit_expression(call->target, op.precedence, false);
    output() << '(';
    for (unsigned ii = 0, ie = call->n_args; ii != ie; ++ii) {
      if (ii) output() << ", ";
      emit_expression(call->args[ii]);
    }
    output() << ')';
    break;
  }
    
  case c_expr_subscript: {
    CExpressionBinary *binary = checked_cast<CExpressionBinary*>(expression);
    emit_expression(binary->left, op.precedence, false);
    output() << '[';
    emit_expression(binary->right);
    output() << ']';
    break;
  }
  
  case c_expr_literal:
    output() << checked_cast<CExpressionLiteral*>(expression)->str;
    break;
  
  case c_expr_array_value:
  case c_expr_struct_value: {
    CExpressionAggregateValue *agg = checked_cast<CExpressionAggregateValue*>(expression);
    output() << '{';
    for (unsigned ii = 0, ie = agg->n_members; ii != ie; ++ii) {
      if (ii) output() << ", ";
      emit_expression(agg->members[ii]);
    }
    output() << '}';
    break;
  }
  
  case c_expr_union_value: {
    CExpressionUnionValue *agg = checked_cast<CExpressionUnionValue*>(expression);
    output() << '{';
    if (c_compiler().has_designated_initializer)
      output() << ".0" << " = ";
    emit_expression(agg->value);
    output() << '}';
    break;
  }
    
  case c_expr_load:
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg, precedence, is_right);
    break;
    
  case c_expr_cast:
    output().put('(');
    emit_type_prolog(expression->type);
    emit_type_epilog(expression->type);
    output().put(')');
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg, op.precedence, true);
    break;
    
  case c_expr_return:
    output() << "return ";
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg);
    output() << ";\n";
    break;
    
  case c_expr_goto:
    output() << "goto ";
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg);
    output() << ";\n";
    break;
    
  case c_expr_if: {
    CExpressionTernary *ter = checked_cast<CExpressionTernary*>(expression);
    output() << "if (";
    emit_expression(ter->first);
    output() << ") {";
    emit_expression(ter->second);
    output() << '}';
    if (ter->third) {
      output() << " else {";
      emit_expression(ter->third);
      output() << '}';
    }
    break;
  }
  
  case c_expr_ternary: {
    CExpressionTernary *ter = checked_cast<CExpressionTernary*>(expression);
    emit_expression(ter->first, op.precedence, false);
    output() << " ? ";
    emit_expression(ter->second);
    output() << " : ";
    emit_expression(ter->third, op.precedence, true);
    break;
  }

  case c_expr_unreachable: c_compiler().emit_unreachable(*this); break;
  case c_expr_block_begin: output() << "{\n"; break;
  case c_expr_block_end: output() << "}\n"; break;
  
  case c_expr_unary: {
    CExpressionUnary *unary = checked_cast<CExpressionUnary*>(expression);
    output() << op.operator_str;
    // Avoid consecutive unary operators printing two characters which become one token
    if (c_operators[unary->arg->op].type == c_expr_unary)
      output() << ' ';
    emit_expression(unary->arg, op.precedence, true);
    break;
  }
  
  case c_expr_binary: {
    CExpressionBinary *binary = checked_cast<CExpressionBinary*>(expression);
    emit_expression(binary->left, op.precedence, false);
    output() << ' ' << op.operator_str << ' ';
    emit_expression(binary->right, op.precedence, true);
    break;
  }
  
  case c_expr_member: {
    CExpressionBinaryIndex *member = checked_cast<CExpressionBinaryIndex*>(expression);
    emit_expression(member->arg, op.precedence, false);
    output() << op.operator_str;
    output() << '\0';
    break;
  }
  
  default: PSI_FAIL("unknown C expression type");
  }
  
  if (has_brackets) output().put(')');
}

/**
 * \brief Emit an expression.
 * 
 * Uses the name of the expression if it has one.
 */
void CModuleEmitter::emit_expression(CExpression *expression, unsigned precedence, bool is_right) {
  if (expression->name.prefix) {
    output() << expression->name;
  } else {
    emit_expression_def(expression, precedence, is_right);
  }
}

/**
 * \brief Write a module to a standard output stream.
 * 
 * Note that this function changes the locale associated with \c output to the C locale, to
 * ensure that no unusual formatting occurs. The number format mode may also be modified.
 */
void CModuleEmitter::run() {
  output().imbue(std::locale::classic());

  emit_types();
  
  for (SinglyLinkedList<CGlobal>::iterator ii = m_module->globals().begin(), ie = m_module->globals().end(); ii != ie; ++ii) {
    emit_location(*ii->location);
    emit_declaration(*ii);
    output() << ";\n";
  }
  
  for (SinglyLinkedList<CGlobal>::iterator ii = m_module->globals().begin(), ie = m_module->globals().end(); ii != ie; ++ii)
    emit_definition(*ii);
}

/// Name any types which require names and are not currently named
void CModule::name_types() {
  for (SinglyLinkedList<CType>::iterator ii = m_types.begin(), ie = m_types.end(); ii != ie; ++ii) {
  }
}

void CModule::emit(std::ostream& output) {
  name_types();
  CModuleEmitter emitter(&output, this);
  emitter.run();
}
}
}
}
