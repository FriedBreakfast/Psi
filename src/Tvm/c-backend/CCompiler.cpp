#include "Builder.hpp"
#include "CModule.hpp"
#include "../../Platform.hpp"

#include <boost/format.hpp>
#include <boost/make_shared.hpp>

namespace Psi {
namespace Tvm {
namespace CBackend {
CCompiler::CCompiler() {
  has_variable_length_arrays = false;
  has_designated_initializer = false;
}

void CCompiler::emit_alignment(CModuleEmitter& PSI_UNUSED(emitter), unsigned PSI_UNUSED(alignment)) {
}

/**
 * \brief Emit an unreachable statement.
 * 
 * \return True if such a statement is supported (and thus emitted successfully).
 */
bool CCompiler::emit_unreachable(CModuleEmitter& PSI_UNUSED(emitter)) {
  return false;
}

namespace {
class AttributeWriter {
  std::ostream *m_output;
  const char *m_start_str, *m_end_str;
  bool m_started;
  
public:
  AttributeWriter(CModuleEmitter& emitter, const char *start, const char *end)
  : m_output(&emitter.output()), m_start_str(start), m_end_str(end), m_started(false) {
  }
  
  std::ostream& next() {
    if (m_started) {
      *m_output << ',';
    } else {
      *m_output << m_start_str;
      m_started = true;
    }
    return *m_output;
  }
  
  void done() {
    if (m_started)
      *m_output << m_end_str;
  }
};
}

class CCompilerMSVC : public CCompiler {
  IntegerType::Width m_pointer_width;
  
public:
  CCompilerMSVC(IntegerType::Width pointer_width)
  : m_pointer_width(pointer_width) {
  }
  
  virtual void emit_alignment(CModuleEmitter& emitter, unsigned n) {
    emitter.output() << "__declspec(align(" << n << ")) ";
  }
  
  virtual bool emit_unreachable(CModuleEmitter& emitter) {
    emitter.output() << "__assume(0)";
    return true;
  }
  
  void declspec_next(CModuleEmitter& emitter, bool& started) {
    if (started) {
      emitter.output() << ',';
    } else {
      emitter.output() << "__declspec(";
      started = true;
    }
  }
  
  void declspec_end(CModuleEmitter& emitter, bool& started) {
    if (started)
      emitter.output() << ')';
  }
  
  virtual void emit_function_attributes(CModuleEmitter& emitter, CFunction *function) {
    AttributeWriter aw(emitter, "__declspec(", ")");
    
    if (function->is_external)
      aw.next() << "dllimport";
    else if (!function->is_private)
      aw.next() << "dllexport";
    
    if (function->alignment) aw.next() << "align(" << function->alignment << ")";

    aw.done();
  }
  
  virtual void emit_global_variable_attributes(CModuleEmitter& emitter, CGlobalVariable* gvar) {
    AttributeWriter aw(emitter, "__declspec(", ")");
    
    if (!gvar->value)
      aw.next() << "dllimport";
    else if (!gvar->is_private)
      aw.next() << "dllexport";

    if (gvar->alignment) aw.next() << "align(" << gvar->alignment << ")";
    
    aw.done();
  }

  virtual const char *integer_type(CModule&, IntegerType::Width width, bool is_signed) {
    IntegerType::Width my_width = (width == IntegerType::iptr) ? m_pointer_width : width;
    switch (my_width) {
    case IntegerType::i8: return is_signed ? "char" : "unsigned char";
    case IntegerType::i16: return is_signed ? "short" : "unsigned short";
    case IntegerType::i32: return is_signed ? "int" : "unsigned int";
    case IntegerType::i64: return is_signed ? "__int64" : "unsigned __int64";
    case IntegerType::i128: return is_signed ? "__int128" : "unsigned __int128";
    default: PSI_FAIL("Unrecognised integer width");
    }
  }

  virtual const char *float_type(CompileErrorPair& err_loc, CModule&, FloatType::Width width) {
    switch (width) {
    case FloatType::fp32: return "float";
    case FloatType::fp64: return "double";
    case FloatType::fp128: err_loc.error_throw("MSVC does not support 128-bit float types");
    case FloatType::fp_x86_80: err_loc.error_throw("MSVC does not support 80-bit X86 extended precision float");
    case FloatType::fp_ppc_128: err_loc.error_throw("MSVC does not support 128-bit PPC extended precision float");
    default: PSI_FAIL("Unrecognised float width");
    }
  }
};

/**
 * Base class for compilers which implement GCC \c __attribute__ extension.
 */
class CCompilerGCCLike : public CCompiler {
public:
  IntegerType::Width pointer_width;
  bool has_float_128;
  bool has_float_80;
  
  CCompilerGCCLike(IntegerType::Width pointer_width_)
  : pointer_width(pointer_width_),
  has_float_128(false),
  has_float_80(false) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }

  virtual void emit_alignment(CModuleEmitter& emitter, unsigned n) {
    emitter.output() << "__attribute__((aligned(" << n << "))) ";
  }
  
  /// \todo Emit calling convention
  virtual void emit_function_attributes(CModuleEmitter& emitter, CFunction *function) {
    AttributeWriter aw(emitter, "__attribute__((", "))");
    if (function->alignment) aw.next() << "aligned(" << function->alignment << ")";
    aw.done();
  }
  
  virtual void emit_global_variable_attributes(CModuleEmitter& emitter, CGlobalVariable *gvar) {
    AttributeWriter aw(emitter, "__attribute__((", "))");
    if (gvar->alignment) aw.next() << "aligned(" << gvar->alignment << ")";
    aw.done();
  }

  virtual const char *integer_type(CModule&, IntegerType::Width width, bool is_signed) {
    IntegerType::Width my_width = (width == IntegerType::iptr) ? pointer_width : width;
    switch (my_width) {
    case IntegerType::i8: return is_signed ? "char" : "unsigned char";
    case IntegerType::i16: return is_signed ? "short" : "unsigned short";
    case IntegerType::i32: return is_signed ? "int" : "unsigned int";
    case IntegerType::i64: return is_signed ? "long long" : "unsigned long long";
    case IntegerType::i128: return is_signed ? "__int128" : "unsigned __int128";
    default: PSI_FAIL("Unrecognised integer width");
    }
  }
  
  virtual const char *int_suffix(CModule&, IntegerType::Width width, bool is_signed) {
    IntegerType::Width my_width = (width == IntegerType::iptr) ? pointer_width : width;
    switch (my_width) {
    case IntegerType::i8: return is_signed ? "char" : "unsigned char";
    case IntegerType::i16: return is_signed ? "short" : "unsigned short";
    case IntegerType::i32: return is_signed ? "int" : "unsigned int";
    case IntegerType::i64: return is_signed ? "LL" : "ULL";
    // GCC doesn't support 128-bit integer constants, so we try for "long long" and hope the constant isn't too large
    case IntegerType::i128: return is_signed ? "LL" : "ULL";
    default: PSI_FAIL("Unrecognised integer width");
    }
  }

  virtual const char *float_type(CModule& module, FloatType::Width width) {
    switch (width) {
    case FloatType::fp32: return "float";
    case FloatType::fp64: return "double";
    case FloatType::fp_ppc_128: module.error_context().error_throw(module.location(), "C compiler does not support 128-bit PPC extended precision float");

    case FloatType::fp128:
      if (has_float_128)
        return "__float128";
      else
        module.error_context().error_throw(module.location(), "C compiler does not support 128-bit float types");
      
    case FloatType::fp_x86_80:
      if (has_float_80)
        return "__float80";
      else
        module.error_context().error_throw(module.location(), "C compiler does not support 80-bit X86 extended precision float");
      
    default: PSI_FAIL("Unrecognised float width");
    }
  }
  
  virtual const char* float_suffix(CModule& module, FloatType::Width width) {
    switch (width) {
    case FloatType::fp32: return "f";
    case FloatType::fp64: return "";
    case FloatType::fp_ppc_128: module.error_context().error_throw(module.location(), "C compiler does not support 128-bit PPC extended precision float");
    case FloatType::fp128: return "q";
    case FloatType::fp_x86_80: return "w";
    default: PSI_FAIL("Unrecognised float width");
    }
  }
};

class CCompilerGCC : public CCompilerGCCLike {
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerGCC(unsigned major, unsigned minor, IntegerType::Width pointer_width)
  : CCompilerGCCLike(pointer_width), m_major_version(major), m_minor_version(minor) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }
  
  /**
   * Check whether the target version of GCC is the specified version, or a later one.
   */
  bool has_version(unsigned major, unsigned minor) {
    return (m_major_version > major) || ((m_major_version == major) && (m_minor_version >= minor));
  }
  
  virtual bool emit_unreachable(CModuleEmitter& emitter) {
    if (has_version(4,5)) {
      emitter.output() << "__builtin_unreachable()";
      return true;
    } else {
      return false;
    }
  }
  
  static boost::shared_ptr<CCompiler> detect(CompileErrorPair& err_loc, const std::string& path) {
    const char *src =
    "__GNUC__\n"
    "__GNUC_MINOR__\n"
    "__SIZEOF_POINTER__\n";
    
    std::vector<std::string> command;
    command.push_back(path);
    command.push_back("-E");
    command.push_back("-");
    
    std::string output = cmd_communicate(err_loc, command, src).first;
  }
};

class CCompilerTCC : public CCompilerGCCLike {
public:
  CCompilerTCC(IntegerType::Width pointer_width) : CCompilerGCCLike(pointer_width) {}
};

class CCompilerClang : public CCompilerGCCLike {
public:
  CCompilerClang(IntegerType::Width pointer_width) : CCompilerGCCLike(pointer_width) {}

  virtual bool emit_unreachable(CModuleEmitter& emitter) {
    emitter.output() << "__builtin_unreachable()";
    return true;
  }
};

namespace {
  enum CompilerType {
    cc_unknown,
    cc_gcc,
    cc_clang,
    cc_tcc,
    cc_msvc
  };
}

/**
 * Try to locate a C compiler on the system.
 */
boost::shared_ptr<CCompiler> detect_c_compiler(CompileErrorPair& err_loc) {
  const char *cc_path = std::getenv("PSI_TVM_CC");
  if (!cc_path)
    cc_path = PSI_TVM_CC;
  
  if (PSI_TVM_CC_TCCLIB && std::strcmp(cc_path, "tcclib"))
    PSI_NOT_IMPLEMENTED();
  
  // Try to identify the compiler by its executable name
  boost::optional<std::string> cc_full_path = Platform::find_in_path(cc_path);
  if (!cc_full_path)
    err_loc.error_throw(boost::format("C compiler not found: %s") % cc_path);
  
  std::string filename = Platform::filename(*cc_full_path);
  CompilerType type = cc_unknown;
  if (filename.find("gcc") != std::string::npos)
    type = cc_gcc;
  else if (filename.find("clang") != std::string::npos)
    type = cc_clang;
  else if (filename.find("tcc") != std::string::npos)
    type = cc_tcc;
  else if (filename.find("cl.exe") != std::string::npos)
    type = cc_msvc;
  
  boost::shared_ptr<CCompiler> result;
  
  if (!result && ((type == cc_unknown) || (type == cc_gcc)))
    result = CCompilerGCC::detect(err_loc, *cc_full_path);
  
  if (!result && ((type == cc_unknown) || (type == cc_clang))) {}
  if (!result && ((type == cc_unknown) || (type == cc_tcc))) {}
  if (!result && ((type == cc_unknown) || (type == cc_msvc))) {}
  
  if (!result)
    err_loc.error_throw(boost::format("Could not identify C compiler: %s") % *cc_full_path);
  
  return result;
}
}
}
}
