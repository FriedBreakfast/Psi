
#include "Builder.hpp"
#include "CModule.hpp"

#include <algorithm>
#include <locale>
#include <cstdio>
#include <cstring>

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
#include "COperators.hpp"
#undef PSI_TVM_C_OP_STR
#undef PSI_TVM_C_OP
};

CExpressionBuilder::CExpressionBuilder(CModule* module, CFunction* function)
: m_module(module), m_function(function) {
}

void CExpressionBuilder::append(const SourceLocation* location, CExpression *expr, bool insert) {
  expr->name.prefix = NULL;
  expr->location = location;
  expr->requires_name = false;
  if (insert && m_function)
    m_function->instructions.append(expr);
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
  CExpressionMember *sub = m_module->pool().alloc<CExpressionMember>();
  if (op == c_op_member)
    sub->aggregate_type = checked_cast<CTypeAggregate*>(arg->type);
  else
    sub->aggregate_type = checked_cast<CTypeAggregate*>(checked_cast<CTypePointer*>(arg->type)->target);
  
  PSI_ASSERT(index < sub->aggregate_type->n_members);
  sub->op = op;
  sub->type = sub->aggregate_type->members[index].type;
  sub->arg = arg;
  sub->index = index;
  sub->eval = c_eval_never;
  if (sub->type->type == c_type_array) {
    sub->type = pointer_type(checked_cast<CTypeArray*>(sub->type)->member);
    sub->lvalue = false;
  } else {    
    sub->lvalue = true;
  }
  append(location, sub);
  return sub;
}

CExpression* CExpressionBuilder::parameter(const SourceLocation* location, CType *type) {
  CExpression *sub = m_module->pool().alloc<CExpression>();
  sub->lvalue = false;
  sub->type = type;
  sub->op = c_op_parameter;
  sub->eval = c_eval_never;
  append(location, sub, false);
  PSI_ASSERT(m_function);
  m_function->parameters.append(sub);
  sub->requires_name = true; // Parameters must have names in C
  return sub;
}

CExpression* CExpressionBuilder::declare(const SourceLocation* location, CType *type, COperatorType op, CExpression *arg, unsigned index) {
  CExpressionBinaryIndex *sub = m_module->pool().alloc<CExpressionBinaryIndex>();
  if (op == c_op_vardeclare) {
    sub->type = pointer_type(type);
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
  sub->requires_name = true;
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
  CExpressionCall *call = m_module->pool().alloc_varstruct<CExpressionCall, CExpression*>(n_args);
  if (target->lvalue)
    call->type = checked_cast<CTypeFunction*>(target->type)->result_type;
  else
    call->type = checked_cast<CTypeFunction*>(checked_cast<CTypePointer*>(target->type)->target)->result_type;
  call->op = c_op_call;
  call->target = target;
  call->n_args = n_args;
  call->eval = (conditional ? c_eval_never : c_eval_write);
  call->lvalue = false;
  std::copy(args, args+n_args, call->args);
  append(location, call);
  return call;
}

CExpression* CExpressionBuilder::aggregate_value(const SourceLocation* location, COperatorType op, CType *ty, unsigned n_members, CExpression *const* members) {
  PSI_ASSERT(n_members == ((op == c_op_array_value) ? checked_cast<CTypeArray*>(ty)->length : checked_cast<CTypeAggregate*>(ty)->n_members));
  CExpressionAggregateValue *agg = m_module->pool().alloc_varstruct<CExpressionAggregateValue, CExpression*>(n_members);
  agg->type = ty;
  agg->op = op;
  agg->n_members = n_members;
  agg->eval = c_eval_read;
  agg->lvalue = false;
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
  agg->lvalue = false;
  agg->eval = c_eval_read;
  append(location, agg);
  return agg;
}

CExpression* CExpressionBuilder::cast(const SourceLocation* location, CType *ty, CExpression *arg) {
  CExpressionUnary *cast = m_module->pool().alloc<CExpressionUnary>();
  cast->type = ty;
  cast->op = c_op_cast;
  cast->arg = arg;
  cast->eval = c_eval_pure;
  cast->lvalue = false;
  append(location, cast);
  return cast;
}

CExpression* CExpressionBuilder::nullary(const SourceLocation* location, COperatorType op, bool insert) {
  CExpression *expr = m_module->pool().alloc<CExpression>();
  expr->type = NULL;
  expr->eval = c_eval_write;
  expr->op = op;
  expr->lvalue = false;
  append(location, expr, insert);
  return expr;
}

void CExpressionBuilder::append(CType *type, const SourceLocation* location, const char *prefix) {
  type->name.prefix = prefix;
  type->name.index = 0;
  type->location = location;
  type->ptr = NULL;
  type->name_used = false;
  m_module->types().append(type);
}

CType* CExpressionBuilder::void_type() {
  CType *ty = m_module->pool().alloc<CType>();
  ty->type = c_type_void;
  append(ty, NULL, "void");
  return ty;
}

CType* CExpressionBuilder::builtin_type(const char *name) {
  CType *ty = m_module->pool().alloc<CType>();
  ty->type = c_type_builtin;
  append(ty, NULL, name);
  return ty;
}

CType* CExpressionBuilder::pointer_type(CType *target) {
  if (target->ptr)
    return target->ptr;
  
  CTypePointer *tu = m_module->pool().alloc<CTypePointer>();  
  tu->type = c_type_pointer;
  tu->target = target;
  append(tu, NULL);
  target->ptr = tu;
  return tu;
}

CType* CExpressionBuilder::array_type(CType *member, unsigned length) {
  CTypeArray *arr = m_module->pool().alloc<CTypeArray>();
  arr->type = c_type_array;
  arr->member = member;
  arr->length = length;
  append(arr, NULL);
  return arr;
}

CType* CExpressionBuilder::function_type(const SourceLocation* location, CType *result_ty, unsigned n_args, const CTypeFunctionArgument *args) {
  CTypeFunction *f = m_module->pool().alloc_varstruct<CTypeFunction, CTypeFunctionArgument>(n_args);
  f->type = c_type_function;
  f->result_type = result_ty;
  f->n_args = n_args;
  std::copy(args, args+n_args, f->args);
  append(f, location);
  return f;
}

CType* CExpressionBuilder::aggregate_type(const SourceLocation* location, CTypeType op, unsigned n_members, const CTypeAggregateMember *members) {
  CTypeAggregate *agg = m_module->pool().alloc_varstruct<CTypeAggregate, CTypeAggregateMember>(n_members);
  agg->type = op;
  agg->n_members = n_members;
  std::copy(members, members+n_members, agg->members);
  append(agg, location);
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
 * 
 * \param with_space Emit a space if one is required before another identifier.
 * \param dont_use_name Print definition even if the type has a name.
 */
void CModuleEmitter::emit_type_prolog(CType *type, bool with_space, bool dont_use_name) {
  if (type->name.prefix && !dont_use_name) {
    output() << type->name;
    if (with_space)
      output() << ' ';
  } else {
    switch (type->type) {
    case c_type_array: {
      CTypeArray *arr = checked_cast<CTypeArray*>(type);
      emit_type_prolog(arr->member, with_space);
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
      emit_type_prolog(ftype->result_type, with_space);
      break;
    }
      
    default: PSI_FAIL("C type should be named; it cannot be printed directly");
    }
  }
}

/**
 * Print part of a type which follows its name.
 */
void CModuleEmitter::emit_type_epilog(CType *type, bool dont_use_name) {
  if (!type->name.prefix || dont_use_name) {
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
        emit_type_prolog(arg.type, false);
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
    if (m_file)
      emit_string(m_file->url.c_str());
    output() << '\"';
  }
  output() << '\n';
}

/**
 * \brief Print a string with escapes.
 */
void CModuleEmitter::emit_string(const char *s) {
  const char *escape_ascii = "\a\b\t\n\v\f\r\"\'\\";
  const char *escape_code = "abtnvfr\"\'\\";
  
  for (const char *p = s; *p; ++p) {
    unsigned char c = *p;
    // This assumes that c is ASCII encoded
    
    if (const char *escapes_p = std::strchr(escape_ascii, c)) {
      // Handle standard escape characters
      output().put('\\').put(escape_code[escapes_p-escape_ascii]);
    } else if (c == '?') {
      // Avoid trigraph interpretation
      if ((p != s) && (p[-1] == '?'))
        output().put('\\');
      output().put('?');
    } else if ((c >= 32) && (c <= 126)) {
      // Printable characters
      output().put(c);
    } else {
      // Print octal code
      char data[5];
      // If the next character is a digit, force a 3 digit octal escape to
      // avoid the next character being interpreted as part of the escape sequence
      unsigned width = ((p[1] >= '0') && (p[1] >= '9')) ? 3 : 1;
      unsigned count = std::sprintf(data, "\\%*o", width, c);
      PSI_ASSERT(count < 5);
      output().write(data, count);
    }
  }
}

/// Emit type declarations
void CModuleEmitter::emit_types() {
  for (SinglyLinkedList<CType>::iterator ii = m_module->types().begin(), ie = m_module->types().end(); ii != ie; ++ii) {
    CType& ty = *ii;
    if (!ty.name_used)
      continue;

    switch (ty.type) {
    case c_type_builtin:
    case c_type_pointer:
    case c_type_array:
    case c_type_void:
      break;
      
    case c_type_function: {
      emit_location(*ty.location);
      output() << "typedef ";
      emit_type_prolog(&ty, true, true);
      output() << ty.name;
      emit_type_epilog(&ty, true);
      output() << ";\n";
      break;
    }

    case c_type_union:
    case c_type_struct: {
      emit_location(*ty.location);
      CTypeAggregate& agg = checked_cast<CTypeAggregate&>(ty);
      output() << "typedef " << (ty.type == c_type_union ? "union" : "struct") << " {\n";
      for (unsigned ii = 0, ie = agg.n_members; ii != ie; ++ii) {
        if (agg.members[ii].name.prefix) {
          emit_type_prolog(agg.members[ii].type, true);
          output() << agg.members[ii].name;
          emit_type_epilog(agg.members[ii].type);
          output() << ";\n";
        }
      }
      output() << "} " << ty.name << ";\n";
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
void CModuleEmitter::emit_declaration(CGlobal& global, bool no_extern) {
  CGlobalVariable *global_var = NULL;
  if (global.linkage == link_local)
    output() << "static ";
  else if (!no_extern)
    output() << "extern ";

  if (global.alignment) c_compiler().emit_alignment(*this, global.alignment);
  if (global.op == c_op_global_variable) {
    global_var = checked_cast<CGlobalVariable*>(&global);
    if (global_var->is_const) output() << "const ";
  }
  
  emit_type_prolog(global.type, true, global.op == c_op_function);
  output() << global.name;
  emit_type_epilog(global.type, global.op == c_op_function);
}

/// Emit global variable or function definition
void CModuleEmitter::emit_definition(CGlobal& global) {
  emit_location(*global.location);
  if (global.op == c_op_global_variable) {
    CGlobalVariable& gvar = checked_cast<CGlobalVariable&>(global);
    if (gvar.value) {
      emit_declaration(gvar, true);
      output() << " = ";
      emit_expression(gvar.value, EmitFlags().initializer(true));
      output() << ";\n";
    }
  } else {
    PSI_ASSERT(global.op == c_op_function);
    CFunction& func = checked_cast<CFunction&>(global);
    if (!func.is_external) {
      m_module->name_locals(&func);
      CTypeFunction *ftype = checked_cast<CTypeFunction*>(func.type);
      emit_type_prolog(ftype->result_type, true);
      output() << func.name;
      output() << '(';
      unsigned arg_index = 0;
      for (SinglyLinkedList<CExpression>::iterator ii = func.parameters.begin(); arg_index != ftype->n_args; ++ii, ++arg_index) {
        PSI_ASSERT(ii != func.parameters.end());
        if (arg_index) output() << ", ";
        CTypeFunctionArgument& arg = ftype->args[arg_index];
        emit_type_prolog(arg.type, true);
        PSI_ASSERT(ii->name.prefix); // Parameters cannot be anonymous in C
        output() << ii->name;
        emit_type_epilog(arg.type);
      }
      output() << ") {\n";
      for (SinglyLinkedList<CExpression>::iterator ii = func.instructions.begin(), ie = func.instructions.end(); ii != ie; ++ii) {
        if (ii->name.prefix || (ii->eval == c_eval_write))
          emit_statement(&*ii);
      }
      output() << "}\n";
    }
  }
}

/**
 * \brief Emit the definition of an expression.
 * 
 * This will not print the name of the expression.
 */
void CModuleEmitter::emit_expression_def(CExpression *expression, EmitFlags flags) {
  COperatorType op_idx = expression->op;
  const COperator& op = c_operators[op_idx];
  
  bool has_brackets = false;
  if (op.precedence > flags.precedence()) {
    has_brackets = true;
  } else if (op.precedence == flags.precedence()) {
    has_brackets = (flags.right() != op.right_associative);
  }
  
  if (has_brackets) output().put('(');
  
  switch (op.type) {
  case c_expr_call: {
    CExpressionCall *call = checked_cast<CExpressionCall*>(expression);
    emit_expression(call->target, EmitFlags().precedence(op.precedence).right(false));
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
    emit_expression(binary->left, EmitFlags().precedence(op.precedence).right(false));
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
    if (!flags.initializer()) {
      output() << '(';
      emit_type_prolog(agg->type, false);
      emit_type_epilog(agg->type);
      output() << ')';
    }
    output() << '{';
    for (unsigned ii = 0, ie = agg->n_members; ii != ie; ++ii) {
      if (ii) output() << ", ";
      emit_expression(agg->members[ii], EmitFlags().initializer(true));
    }
    output() << '}';
    break;
  }
  
  case c_expr_union_value: {
    CExpressionUnionValue *agg = checked_cast<CExpressionUnionValue*>(expression);
    if (!flags.initializer()) {
      output() << '(';
      emit_type_prolog(agg->type, false);
      emit_type_epilog(agg->type);
      output() << ')';
    }
    output() << '{';
    if (c_compiler().has_designated_initializer) {
      CTypeAggregate *agg_type = checked_cast<CTypeAggregate*>(agg->type);
      output() << '.' << agg_type->members[agg->index].name << " = ";
    }
    emit_expression(agg->value, EmitFlags().initializer(true));
    output() << '}';
    break;
  }
    
  case c_expr_load:
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg, flags);
    break;
    
  case c_expr_cast:
    output().put('(');
    emit_type_prolog(expression->type, false);
    emit_type_epilog(expression->type);
    output().put(')');
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg, EmitFlags().precedence(op.precedence).right(true));
    break;
    
  case c_expr_ternary: {
    CExpressionTernary *ter = checked_cast<CExpressionTernary*>(expression);
    emit_expression(ter->first, EmitFlags().precedence(op.precedence).right(false));
    output() << " ? ";
    emit_expression(ter->second);
    output() << " : ";
    emit_expression(ter->third, EmitFlags().precedence(op.precedence).right(true));
    break;
  }

  case c_expr_unary: {
    CExpressionUnary *unary = checked_cast<CExpressionUnary*>(expression);
    output() << op.operator_str;
    // Avoid consecutive unary operators printing two characters which become one token
    if (c_operators[unary->arg->op].type == c_expr_unary)
      output() << ' ';
    emit_expression(unary->arg, EmitFlags().precedence(op.precedence).right(true));
    break;
  }
  
  case c_expr_binary: {
    CExpressionBinary *binary = checked_cast<CExpressionBinary*>(expression);
    emit_expression(binary->left, EmitFlags().precedence(op.precedence).right(false));
    output() << ' ' << op.operator_str << ' ';
    emit_expression(binary->right, EmitFlags().precedence(op.precedence).right(true));
    break;
  }
  
  case c_expr_member: {
    CExpressionMember *member = checked_cast<CExpressionMember*>(expression);
    emit_expression(member->arg, EmitFlags().precedence(op.precedence).right(false));
    output() << op.operator_str << member->aggregate_type->members[member->index].name;
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
void CModuleEmitter::emit_expression(CExpression *expression, EmitFlags flags) {
  if (expression->name.prefix) {
    output() << expression->name;
  } else {
    emit_expression_def(expression, flags);
  }
}

/**
 * \brief Emit a statement.
 */
void CModuleEmitter::emit_statement(CExpression *expression) {
  switch (expression->op) {
  case c_op_declare: {
    CExpressionBinaryIndex *binary = checked_cast<CExpressionBinaryIndex*>(expression);
    if (binary->index)
      c_compiler().emit_alignment(*this, binary->index);
    emit_type_prolog(binary->type, true);
    output() << binary->name;
    emit_type_epilog(binary->type);
    if (binary->arg) {
      output() << " = ";
      emit_expression(binary->arg, EmitFlags().initializer(true));
    }
    output() << ";\n";
    break;
  }
  
  case c_op_vardeclare: {
    CExpressionBinaryIndex *binary = checked_cast<CExpressionBinaryIndex*>(expression);
    if (binary->index)
      c_compiler().emit_alignment(*this, binary->index);
    CType *inner_type = checked_cast<CTypePointer*>(binary->type)->target;
    emit_type_prolog(inner_type, true);
    output() << binary->name;
    emit_type_epilog(inner_type);
    output() << '[';
    emit_expression(binary->arg);
    output() << ']' << ";\n";
    break;
  }
  
  case c_op_label:
    if (expression->name.prefix)
      output() << expression->name << ":\n";
    break;
  
  case c_op_return: {
    CExpressionUnary *unary = checked_cast<CExpressionUnary*>(expression);
    if (unary->arg) {
      output() << "return ";
      emit_expression(unary->arg);
      output() << ";\n";
    } else {
      output() << "return;\n";
    }
    break;
  }
    
  case c_op_goto:
    output() << "goto ";
    emit_expression(checked_cast<CExpressionUnary*>(expression)->arg);
    output() << ";\n";
    break;
    
  case c_op_if: {
    CExpressionUnary *un = checked_cast<CExpressionUnary*>(expression);
    output() << "if (";
    emit_expression(un->arg);
    output() << ") {\n";
    break;
  }
  
  case c_op_elif: {
    CExpressionUnary *un = checked_cast<CExpressionUnary*>(expression);
    output() << "} else if (";
    emit_expression(un->arg);
    output() << ") {\n";
    break;
  }
  
  case c_op_else: {
    output() << "} else {\n";
    break;
  }
  
  case c_op_unreachable: c_compiler().emit_unreachable(*this); break;
  case c_op_block_begin: output() << "{\n"; break;
  case c_op_endif:
  case c_op_block_end: output() << "}\n"; break;
  
  default:
    if (expression->name.prefix) {
      PSI_ASSERT(expression->type);
      emit_type_prolog(expression->type, true);
      output() << expression->name;
      emit_type_epilog(expression->type);
      output() << " = ";
    }
    emit_expression_def(expression);
    output() << ";\n";
    break;
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
    emit_declaration(*ii, false);
    output() << ";\n";
  }
  
  for (SinglyLinkedList<CGlobal>::iterator ii = m_module->globals().begin(), ie = m_module->globals().end(); ii != ie; ++ii)
    emit_definition(*ii);
}

bool CNameMap::NameCompare::operator () (const CName& lhs, const CName& rhs) const {
  int ord = std::strcmp(lhs.prefix, rhs.prefix);
  if (ord < 0)
    return true;
  else if (ord > 0)
    return false;
  
  if (lhs.index < rhs.index)
    return true;
  else
    return false;
}

CNameMap::CNameMap(WriteMemoryPool *pool) : m_map(NameCompare(), pool) {
}

CNameMap::CNameMap(const CNameMap& src, WriteMemoryPool *pool)
: m_map(src.m_map.begin(), src.m_map.end(), NameCompare(), pool) {
}

CName CNameMap::insert(const char *s, bool ignore_duplicate) {
  const std::locale& c_locale = std::locale::classic();
  
  const char *p = s + std::strlen(s);
  
  // Remove digit string at end of number
  const char *q = p;
  for (; q != s; --q) {
    if (!std::isdigit(q[-1], c_locale))
      break;
  }
  
  // Skip over any zeros
  while (*q == '0')
    ++q;
  
  unsigned index = 0;
  if (*q != '\0')
    std::sscanf(q, "%d", &index);
  
  CName tmp = {s, 0};
  NameMap::iterator it = m_map.upper_bound(tmp);
  if (it != m_map.begin()) {
    --it;
    if ((std::strncmp(it->prefix, s, q-s) == 0) && (it->prefix[q-s] == '\0')) {
      CName result = {it->prefix, index};
      if (ignore_duplicate) {
        m_map.insert(result);
        return result;
      } else {
        while (true) {
          if (m_map.insert(result).second)
            return result;
          ++result.index;
        }
      }
    }
  }
  
  WriteMemoryPool& pool = m_map.get_allocator().pool();
  char *s_copy = pool.str_alloc(q-s+1);
  std::copy(s, q, s_copy);
  s_copy[q-s] = '\0';
  
  CName result = {s_copy, index};
  m_map.insert(result);
  return result;
}

/**
 * \brief Reserve a name.
 * 
 * If this name is already present, return the existing name.
 */
CName CNameMap::reserve(const char *s) {
  return insert(s, true);
}

/**
 * \brief Generate a name.
 * 
 * If this name is a duplicate of an existing name, generate a new name by adding a numeric suffix.
 */
CName CNameMap::get(const char *base) {
  return insert(base, false);
}

CModule::CModule(CCompiler *c_compiler, CompileErrorContext *error_context, const SourceLocation& location)
: m_c_compiler(c_compiler),
m_error_context(error_context),
m_location(location),
m_names(&m_pool) {
}

void CModule::add_global(CGlobal *global, const SourceLocation *location, CType *type, const char *name) {
  global->alignment = 0;
  global->linkage = link_local;
  global->eval = c_eval_write;
  global->lvalue = true;
  global->requires_name = false;
  global->type = type;
  global->location = location;
  global->name = m_names.reserve(name);
  m_globals.append(global);
}

CGlobalVariable *CModule::new_global(const SourceLocation *location, CType *type, const char *name) {
  CGlobalVariable *gvar = m_pool.alloc<CGlobalVariable>();
  gvar->op = c_op_global_variable;
  gvar->is_const = false;
  add_global(gvar, location, type, name);
  return gvar;
}

CFunction *CModule::new_function(const SourceLocation *location, CType *type, const char *name) {
  CFunction *f = m_pool.alloc<CFunction>();
  f->op = c_op_function;
  f->is_external = true;
  add_global(f, location, type, name);
  return f;
}

/// Name any types which require names and are not currently named
void CModule::name_types() {
  for (SinglyLinkedList<CType>::iterator ii = m_types.begin(), ie = m_types.end(); ii != ie; ++ii) {
    if (!ii->name_used)
      continue;

    switch (ii->type) {
    case c_type_struct:
    case c_type_union:
    case c_type_function: {
      PSI_ASSERT(!ii->name.prefix);
      std::string s = location_to_c_identifier(*ii->location, location(), true);
      ii->name = m_names.get(s.c_str());
    }
    
    case c_type_void:
    case c_type_builtin:
      PSI_ASSERT(ii->name.prefix);
      break;
      
    case c_type_pointer:
    case c_type_array:
      PSI_ASSERT(!ii->name.prefix);
      break;
      
    default: PSI_FAIL("unknown c type kind");
    }
  }
}

/**
 * Generate names for function parameters and local variables
 * 
 * Must be called exactly once on a function, and name_types must have been
 * called on the module first.
 */
void CModule::name_locals(CFunction *function) {
  CNameMap local_names(m_names);
  for (SinglyLinkedList<CExpression>::iterator ii = function->parameters.begin(), ie = function->parameters.end(); ii != ie; ++ii) {
    PSI_ASSERT(!ii->name.prefix && ii->requires_name);
    std::string base_name = location_to_c_identifier(*ii->location, *function->location, false);
    ii->name = local_names.get(base_name.c_str());
  }
  
  for (SinglyLinkedList<CExpression>::iterator ii = function->instructions.begin(), ie = function->instructions.end(); ii != ie; ++ii) {
    PSI_ASSERT(!ii->name.prefix);
    if (ii->requires_name) {
      PSI_ASSERT(ii->eval != c_eval_never);
      std::string base_name = location_to_c_identifier(*ii->location, *function->location, false);
      ii->name = local_names.get(base_name.c_str());
    }
  }
}

void CModule::emit(std::ostream& output) {
  name_types();
  CModuleEmitter emitter(&output, this);
  emitter.run();
}

namespace {
/**
 * List of C keywords.
 * 
 * Not all of these are in the C standard: various extensions are presumed.
 * This must be maintained in alphabetical order so it can be scanned by a binary search.
 */
const char *c_keywords[] = {
  "auto",
  "asm",
  "break",
  "case",
  "char",
  "const",
  "continue",
  "default",
  "do",
  "double",
  "else",
  "enum",
  "extern",
  "float",
  "for",
  "goto",
  "if",
  "inline",
  "int",
  "long",
  "register",
  "restrict",
  "return",
  "short",
  "signed",
  "sizeof",
  "static",
  "struct",
  "switch",
  "typedef",
  "typeof",
  "union",
  "unsigned",
  "void",
  "volatile",
  "while"
};

struct StringLess {
  bool operator () (const std::string& s1, const std::string& s2) const {return s1 < s2;}
  bool operator () (const char *s1, const char *s2) const {return std::strcmp(s1,s2) < 0;}
  bool operator () (const char *s1, const std::string& s2) const {return s1 < s2;}
  bool operator () (const std::string& s1, const char *s2) const {return s1 < s2;}
};
}

/**
 * \brief Turn a SourceLocation into a C identifier.
 * 
 * This does not attempt name mangling. Rather it tries to turn the
 * location into a reasonably human readable string, and ensuring
 * the name is unique is done elsewhere.
 * 
 * \param is_global Whether the name will be used at global scope.
 */
std::string location_to_c_identifier(const SourceLocation& location, const SourceLocation& context, bool is_global) {
  std::string base = location.logical->error_name(context.logical, true);
  // Ensure we have a valid C identifier
  std::string output;
  output.reserve(base.size());
  const std::locale& c_locale = std::locale::classic();
  for (std::string::const_iterator ii = base.begin(), ie = base.end(); ii != ie; ++ii) {
    char c = *ii;
    if (std::isalnum(c, c_locale)) {
      output.push_back(c);
    } else if (c == '_') {
      // Prevent two consecutive underscores and underscores at the start of global identitiers
      if ((!is_global && output.empty()) || (output[output.length()-1] != '_'))
        output.push_back('_');
    }
  }
  
  if (output.empty())
    return "x"; // Generic unknown idenifier
  
  // Is the first character a number?
  if (std::isdigit(output[0], c_locale))
    output.insert(output.begin(), 'x');
  // Does the identifier start with an underscore followed by a capital letter
  else if ((output.size() >= 2) && (output[0] == '_') && std::isupper(output[1], c_locale))
    output.insert(output.begin(), 'x');
  
  // Is this a keyword?
  while (std::binary_search(c_keywords, c_keywords+array_size(c_keywords), output, StringLess()))
    output.insert(output.begin(), 'x');
  
  return output;
}
}
}
}
