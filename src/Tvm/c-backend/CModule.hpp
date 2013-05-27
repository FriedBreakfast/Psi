#ifndef HPP_PSI_TVM_CMODULE
#define HPP_PSI_TVM_CMODULE

#include "../../CppCompiler.hpp"
#include "../../Utility.hpp"
#include "../../SourceLocation.hpp"
#include "../Core.hpp"

#include <set>
#include <boost/intrusive/list.hpp>
#include <boost/iterator/iterator_facade.hpp>

/**
 * \file
 * 
 * General framework for creating C modules.
 */

namespace Psi {

class CompileErrorContext;
namespace Tvm {
namespace CBackend {
PSI_SMALL_ENUM(CTypeType) {
  c_type_builtin,
  c_type_struct,
  c_type_union,
  c_type_function,
  c_type_pointer,
  c_type_array,
  c_type_void
};

/**
 * General operator category.
 */
PSI_SMALL_ENUM(CExpressionType) {
  c_expr_unary, /// Unary operator
  c_expr_binary, /// Binary operator
  c_expr_member,
#define PSI_TVM_C_OP_STR(op,ty,prev,right_assoc,str)
#define PSI_TVM_C_OP(op,prec,right_assoc) c_expr_##op,
#include "COperators.hpp"
#undef PSI_TVM_C_OP_STR
#undef PSI_TVM_C_OP
};

struct COperator {
  CExpressionType type;
  unsigned precedence;
  bool right_associative; ///< Whether this operator is right rather than left associative (does not apply to unary operators)
  const char *operator_str; ///< C operator string (without spaces)
};

extern const COperator c_operators[];

PSI_SMALL_ENUM(COperatorType) {
#define PSI_TVM_C_OP(op,prec,right_assoc) c_op_##op,
#define PSI_TVM_C_OP_STR(op,ty,prec,right_assoc,str) c_op_##op,
#include "COperators.hpp"
#undef PSI_TVM_C_OP_STR
#undef PSI_TVM_C_OP
};

class SinglyLinkedListBase : public NonCopyable {
  template<typename> friend class SinglyLinkedList;
  SinglyLinkedListBase *m_next;
public:
  SinglyLinkedListBase() : m_next(NULL) {}
};

template<typename T>
class SinglyLinkedList {
  SinglyLinkedListBase *m_first, *m_last;
  
public:
  SinglyLinkedList() : m_first(NULL), m_last(NULL) {}
  
  void append(T *ptr) {
    SinglyLinkedListBase *base = ptr;
    PSI_ASSERT(!base->m_next);
    if (!m_first) {
      m_first = m_last = base;
    } else {
      m_last->m_next = base;
      m_last = base;
    }
  }
  
  bool empty() const {return !m_first;}
  
  class iterator : public boost::iterator_facade<iterator, T, boost::forward_traversal_tag> {
    SinglyLinkedListBase *m_ptr;

  public:
    iterator() : m_ptr(NULL) {}
    
  private:
    friend class SinglyLinkedList;
    explicit iterator(SinglyLinkedListBase *ptr) : m_ptr(ptr) {}
    friend class boost::iterator_core_access;
    
    bool equal(const iterator& other) const {return m_ptr == other.m_ptr;}
    void increment() {m_ptr = m_ptr->m_next;}
    T& dereference() const {return *static_cast<T*>(m_ptr);}
  };
  
  iterator begin() {return iterator(m_first);}
  iterator end() {return iterator();}
};

struct CTypePointer;

struct CName {
  const char *prefix;
  unsigned index;
};

class CNameMap {
  struct NameCompare {bool operator () (const CName&, const CName&) const;};
  typedef std::set<CName, NameCompare, WriteMemoryPoolAllocator<CName> > NameMap; 
  NameMap m_map;

  CName insert(const char *fullname, bool ignore_duplicate);
  
public:
  CNameMap(WriteMemoryPool *pool);
  CNameMap(const CNameMap& src, WriteMemoryPool *pool);
  
  CName reserve(const char *s);
  CName get(const char *s);
};

std::ostream& operator << (std::ostream& os, const CName& name);

std::string location_to_c_identifier(const SourceLocation& location, const SourceLocation& context, bool is_global);

/**
 * \brief Base class of C source code elements.
 */
struct CElement : SinglyLinkedListBase, CheckedCastBase {
  const SourceLocation* location;
  CName name;
};

struct CType : CElement {
  bool name_used;
  CTypeType type;
  CTypePointer *ptr;
};

struct CTypeArray : CType {
  CType *member;
  unsigned length;
};

struct CTypeFunctionArgument {
  CType *type;
};

struct CTypeFunction : CType {
  CType *result_type;
  unsigned n_args;
  PSI_FLEXIBLE_ARRAY(CTypeFunctionArgument args);
};

struct CTypeAggregateMember {
  CType *type;
  CName name;
};

struct CTypeAggregate : CType {
  unsigned n_members;
  PSI_FLEXIBLE_ARRAY(CTypeAggregateMember members);
};

struct CTypePointer : CType {
  CType *target;
};

/**
 * Evaluation mode of a CExpression.
 */
PSI_SMALL_ENUM(CExpressionEvaluation) {
  c_eval_never, ///< Pure expression which should never be stored in a local variable (usually a literal integer)
  c_eval_pure, ///< Pure expression which may be given a name (it will be if the value is re-used)
  c_eval_read, ///< Reads system state, must be ordered with respect to c_eval_write expressions (and need not be emitted if not used)
  c_eval_write ///< Modifies system state, must be evaluated where specified (and the result named)
};

struct CExpression : CElement {
  CType *type;
  COperatorType op;
  CExpressionEvaluation eval;
  /**
   * If true, this is an lvalue. This stands in for alloca() and globals in C output.
   * In this case, \c type will be the pointed-to type rather than the original
   * type before lowering.
   */
  bool lvalue;
  bool requires_name;
};

struct CExpressionLiteral : CExpression {
  const char *str;
};

struct CExpressionCall : CExpression {
  CExpression *target;
  unsigned n_args;
  PSI_FLEXIBLE_ARRAY(CExpression *args);
};

struct CExpressionAggregateValue : CExpression {
  unsigned n_members;
  PSI_FLEXIBLE_ARRAY(CExpression *members);
};

struct CExpressionUnionValue : CExpression {
  unsigned index;
  CExpression *value;
};

struct CExpressionUnary : CExpression {
  CExpression *arg;
};

struct CExpressionBinary : CExpression {
  CExpression *left, *right;
};

struct CExpressionTernary : CExpression {
  CExpression *first, *second, *third;
};

struct CExpressionBinaryIndex : CExpression {
  CExpression *arg;
  unsigned index;
};

struct CExpressionMember : CExpression {
  CTypeAggregate *aggregate_type;
  CExpression *arg;
  unsigned index;
};

class CModule;
struct CFunction;

class CExpressionBuilder {
  CModule *m_module;
  CFunction *m_function;
  
  void append(const SourceLocation* location, CExpression *expr, bool insert=true);
  void append(CType *type, const SourceLocation* location, const char *prefix=NULL);
  
public:
  CExpressionBuilder(CModule *module, CFunction *function=NULL);
  CModule& module() const {return *m_module;}
  
  const char *strdup(const char *s);
  CExpression* unary(const SourceLocation* location, CType *ty, CExpressionEvaluation eval, COperatorType op, CExpression *arg, bool lvalue=false);
  CExpression* binary(const SourceLocation* location, CType *ty, CExpressionEvaluation eval, COperatorType op, CExpression *left, CExpression *right, bool lvalue=false);
  CExpression* ternary(const SourceLocation* location, CType *ty, CExpressionEvaluation eval, COperatorType op, CExpression *a, CExpression *b, CExpression *c);
  CExpression* member(const SourceLocation* location, COperatorType op, CExpression *arg, unsigned index);
  CExpression* parameter(const SourceLocation* location, CType *type);
  CExpression* declare(const SourceLocation* location, CType *type, COperatorType op, CExpression *arg, unsigned index_or_alignment);
  CExpression* literal(const SourceLocation* location, CType *ty, const char *str);
  CExpression* call(const SourceLocation* location, CExpression *target, unsigned n_args, CExpression *const* args, bool conditional=false);
  CExpression* aggregate_value(const SourceLocation* location, COperatorType op, CType *ty, unsigned n_members, CExpression *const* members);
  CExpression* union_value(const SourceLocation* location, CType *ty, unsigned index, CExpression *value);
  CExpression* cast(const SourceLocation* location, CType *ty, CExpression *arg);
  CExpression* nullary(const SourceLocation* location, COperatorType op, bool insert=true);
  
  CType* void_type();
  CType* builtin_type(const char *name);
  CType* pointer_type(CType *arg);
  CType* array_type(CType *arg, unsigned length);
  CType* function_type(const SourceLocation* location, CType *result_ty, unsigned n_args, const CTypeFunctionArgument *args);
private:
  CType* aggregate_type(const SourceLocation* location, CTypeType op, unsigned n_members, const CTypeAggregateMember *members);
public:
  CType* struct_type(const SourceLocation* location, unsigned n_members, const CTypeAggregateMember *members);
  CType* union_type(const SourceLocation* location, unsigned n_members, const CTypeAggregateMember *members);
};

struct CGlobal : CExpression {
  Linkage linkage;
  /// Alignment. If zero, default alignment is used.
  unsigned alignment;
};

struct CFunction : CGlobal {
  bool is_external;
  SinglyLinkedList<CExpression> parameters;
  SinglyLinkedList<CExpression> instructions;
};

struct CGlobalVariable : CGlobal {
  CType *type;
  CExpression *value;
  bool is_const;
};

class CCompiler;

class CModule {
  CCompiler *m_c_compiler;
  CompileErrorContext *m_error_context;
  SourceLocation m_location;
  WriteMemoryPool m_pool;
  SinglyLinkedList<CType> m_types;
  SinglyLinkedList<CGlobal> m_globals;
  CNameMap m_names;
  
  void add_global(CGlobal *global, const SourceLocation *location, CType *type, const char *name);

public:
  CModule(CCompiler *compiler, CompileErrorContext *error_context, const SourceLocation& location);
  CGlobalVariable *new_global(const SourceLocation *location, CType *type, const char *name);
  CFunction *new_function(const SourceLocation *location, CType *type, const char *name);

  WriteMemoryPool& pool() {return m_pool;}
  const SourceLocation& location() {return m_location;}
  CompileErrorContext& error_context() {return *m_error_context;}
  void emit(std::ostream& output);
  CCompiler& c_compiler() {return *m_c_compiler;}
  void name_types();
  void name_locals(CFunction *function);
  SinglyLinkedList<CType>& types() {return m_types;}
  SinglyLinkedList<CGlobal>& globals() {return m_globals;}
};

class CModuleEmitter {
  CModule *m_module;
  std::ostream *m_output;
  SourceFile *m_file;

  void emit_types();
  void emit_declaration(CGlobal& global, bool no_extern);
  void emit_definition(CGlobal& global);
  
public:
  class EmitFlags {
    unsigned m_precedence;
    bool m_right;
    bool m_initializer;
    
  public:
    EmitFlags() : m_precedence(17), m_right(true), m_initializer(false) {}
    unsigned precedence() const {return m_precedence;}
    EmitFlags precedence(unsigned precedence) const {EmitFlags ef(*this); ef.m_precedence = precedence; return ef;}
    bool right() const {return m_right;}
    EmitFlags right(bool flag) const {EmitFlags ef(*this); ef.m_right = flag; return ef;}
    bool initializer() const {return m_initializer;}
    EmitFlags initializer(bool flag) const {EmitFlags ef(*this); ef.m_initializer = flag; return ef;}
  };
  
  CModuleEmitter(std::ostream *output, CModule *module);
  CCompiler& c_compiler() {return m_module->c_compiler();}
  void run();
  void emit_location(const SourceLocation& location);
  void emit_string(const char *s);
  void emit_type(CType *type);
  void emit_expression(CExpression *expression, EmitFlags flags=EmitFlags());
  void emit_expression_def(CExpression *expression, EmitFlags flags=EmitFlags());
  void emit_statement(CExpression *expression);
  void emit_type_prolog(CType *type, bool with_space, bool dont_use_name=false);
  void emit_type_epilog(CType *type, bool dont_use_name=false);
  std::ostream& output() {return *m_output;}
};

struct CNumberType {
  /// \brief Number type name
  const char *type_name;
  /// \brief Suffix for literals of this type
  const char *literal_suffix;
};
}
}
}

#endif
