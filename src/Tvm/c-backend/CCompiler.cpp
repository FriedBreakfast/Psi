#include "CModule.hpp"

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
public:
  CCompilerMSVC() {
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
    
    if (function->blocks.empty())
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
};

/**
 * Base class for compilers which implement GCC \c __attribute__ extension.
 */
class CCompilerGCCLike : public CCompiler {
public:
  CCompilerGCCLike() {
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
};

class CCompilerGCC : public CCompilerGCCLike {
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerGCC(unsigned major, unsigned minor)
  : m_major_version(major), m_minor_version(minor) {
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
};

class CCompilerTCC : public CCompilerGCCLike {
public:
};

class CCompilerClang : public CCompilerGCCLike {
public:
  virtual bool emit_unreachable(CModuleEmitter& emitter) {
    emitter.output() << "__builtin_unreachable()";
    return true;
  }
};
}
}
}
