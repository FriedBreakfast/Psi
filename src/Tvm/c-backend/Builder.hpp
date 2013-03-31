#ifndef HPP_PSI_TVM_CBACKEND_BUILDER
#define HPP_PSI_TVM_CBACKEND_BUILDER

#include "../Core.hpp"
#include "../Number.hpp"

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
class CModuleEmitter;
class CModule;
struct CFunction;
struct CGlobalVariable;

class CCompiler {
public:
  CCompiler();
  
  /// \brief Has variable length array support
  bool has_variable_length_arrays;
  /// \brief Has designated initializer support
  bool has_designated_initializer;
  
  /// \brief Get a specified integer type name.
  virtual const char *integer_type(CModule& module, IntegerType::Width width, bool is_signed) = 0;
  /// \brief Get a specified float type name.
  virtual const char *float_type(CModule& module, FloatType::Width width) = 0;
  
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
};

class CModuleBuilder {
  CCompiler *m_c_compiler;
  
public:
  CModuleBuilder(CCompiler *c_compiler);
  void run(Module& module);
  void build_function(const ValuePtr<Function>& function);
  void build_global_variable(const ValuePtr<GlobalVariable>& gvar);

  const std::string& build_type(const ValuePtr<>& type);
};
}
}
}

#endif
