#ifndef HPP_PSI_TVM_CBACKEND_BUILDER
#define HPP_PSI_TVM_CBACKEND_BUILDER

#include "../Core.hpp"
#include "../Number.hpp"
#include "../Jit.hpp"
#include "../../Platform.hpp"

#include "CModule.hpp"

namespace Psi {
namespace Tvm {
/**
 * \brief TVM backend which writes C99 code.
 * 
 * Note that the resulting code will not be valid C89 or C++. It also uses
 * features which are "optional" in C11, but mandatory in C99.
 * 
 * <ul>
 * <li>Forward declaration of static variables is not allowed in C++ but works in C.</li>
 * <li>Variably sized arrays are a mandatory C99 feature but an optional C11 one.</li>
 * </ul>
 */
namespace CBackend {
struct PrimitiveType {
  std::string name;
  boost::optional<std::string> suffix;
  unsigned size;
  unsigned alignment;
};

struct PrimitiveTypeSet {
  unsigned pointer_size, pointer_alignment;
  PrimitiveType int_types[IntegerType::i_max];
  PrimitiveType uint_types[IntegerType::i_max];
  PrimitiveType float_types[FloatType::fp_max];
};

class CCompiler {
public:
  CCompiler();
  
  /// \brief Has variable length array support
  bool has_variable_length_arrays;
  /// \brief Has designated initializer support
  bool has_designated_initializer;
  /// \brief Supported primitive types
  PrimitiveTypeSet primitive_types;
  
  /**
   * \brief Emit an alignment attribute.
   * 
   * It is assumed this attribute appears before the variable concerned.
   */
  virtual void emit_alignment(CModuleEmitter& emitter, unsigned n) = 0;
  
  virtual bool emit_unreachable(CModuleEmitter& emitter);

  /// \brief Emit function attributes
  virtual void emit_function_attributes(CModuleEmitter& emitter, CFunction *function) = 0;
  
  /// \brief Emit global variable attributes
  virtual void emit_global_variable_attributes(CModuleEmitter& emitter, CGlobalVariable *gvar) = 0;
  
  /// \brief Compile a program
  virtual void compile_program(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) = 0;
  
  /// \brief Compile a shared library
  virtual void compile_library(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) = 0;
  
  /// \brief Compile and load a shared library
  virtual boost::shared_ptr<Platform::PlatformLibrary> compile_load_library(const CompileErrorPair& err_loc, const std::string& source);
};

/**
 * Converts TVM types to CType. Also handles builtin functions.
 */
class TypeBuilder {
  typedef boost::unordered_map<ValuePtr<>, CType*> TypeMapType;
  TypeMapType m_types;
  
  CType *m_void_type;
  CType *m_signed_integer_types[IntegerType::i_max];
  CType *m_unsigned_integer_types[IntegerType::i_max];
  CType *m_float_types[FloatType::fp_max];

  CExpression *m_psi_alloca, *m_psi_freea, *m_memcpy, *m_memset;
  
  CExpressionBuilder m_c_builder;

  CType* build_function_type(const ValuePtr<FunctionType>& ftype);
  
public:
  TypeBuilder(CModule *module);
  CType* build(const ValuePtr<>& term, bool name_used=true);
  CExpressionBuilder& c_builder() {return m_c_builder;}
  CModule& module() const {return m_c_builder.module();}
  CCompiler& c_compiler() {return c_builder().module().c_compiler();}
  CompileErrorContext& error_context() {return c_builder().module().error_context();};
  
  CType* void_type();
  CType* integer_type(IntegerType::Width width, bool is_signed);
  CType* float_type(FloatType::Width width);

  CExpression *get_psi_alloca();
  CExpression *get_psi_freea();
  CExpression *get_memcpy();
  CExpression *get_memset();
};

class ValueBuilder {
  TypeBuilder *m_type_builder;
  CExpressionBuilder m_c_builder;
  typedef boost::unordered_map<ValuePtr<>, CExpression*> ExpressionMapType;
  ExpressionMapType m_expressions;
  typedef boost::unordered_map<int, CExpression*> IntegerLiteralMapType;
  IntegerLiteralMapType m_integer_literals;
  
public:
  ValueBuilder(TypeBuilder *type_builder);
  ValueBuilder(const ValueBuilder& base, CFunction *function);
  
  TypeBuilder& type_builder() {return *m_type_builder;}
  CType* build_type(const ValuePtr<>& type, bool name_used=true);
  CExpression* integer_literal(int value);
  CExpression* build(const ValuePtr<>& value, bool force_eval=false);
  CExpression* build_rvalue(const ValuePtr<>& value);
  CExpressionBuilder& c_builder() {return m_c_builder;}
  CModule& module() const {return m_c_builder.module();}
  CCompiler& c_compiler() const {return module().c_compiler();}
  CompileErrorContext& error_context() {return c_builder().module().error_context();}
  void put(const ValuePtr<>& key, CExpression *value);
};

class CModuleBuilder {
  CCompiler *m_c_compiler;

  Module *m_module;
  CModule m_c_module;
  TypeBuilder m_type_builder;
  ValueBuilder m_global_value_builder;
  
  void build_function_body(const ValuePtr<Function>& function, CFunction *c_function);

public:
  CModuleBuilder(CCompiler *c_compiler, Module& module);
  std::string run();
};

class CJit : public Jit {
  typedef std::map<Module*, boost::shared_ptr<Platform::PlatformLibrary> > ModuleMap;
  ModuleMap m_modules;
  
public:
  CJit(const boost::shared_ptr<JitFactory>& factory, const boost::shared_ptr<CCompiler>& compiler);
  virtual ~CJit();

  virtual void add_module(Module *module);
  virtual void remove_module(Module *module);
  virtual void* get_symbol(const ValuePtr<Global>& global);

private:
  boost::shared_ptr<CCompiler> m_compiler;
};

boost::shared_ptr<CCompiler> detect_c_compiler(const CompileErrorPair& err_loc);
}
}
}

#endif
