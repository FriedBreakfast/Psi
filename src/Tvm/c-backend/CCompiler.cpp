#include "Builder.hpp"
#include "CModule.hpp"
#include "../../Platform.hpp"

#include <fstream>
#include <boost/format.hpp>
#include <boost/make_shared.hpp>

#ifdef PSI_TVM_CC_TCCLIB
#include <libtcc.h>
#endif

namespace Psi {
namespace Tvm {
namespace CBackend {
CCompiler::CCompiler() {
  has_variable_length_arrays = false;
  has_designated_initializer = false;
}

void CCompiler::emit_alignment(CModuleEmitter& PSI_UNUSED(emitter), unsigned PSI_UNUSED(alignment)) {
}

namespace {
  struct LibraryTempFilePair {
    Platform::TemporaryPath path;
    boost::shared_ptr<Platform::PlatformLibrary> library;
  };
}

boost::shared_ptr<Platform::PlatformLibrary> CCompiler::compile_load_library(const CompileErrorPair& err_loc, const std::string& source) {
  boost::shared_ptr<LibraryTempFilePair> result = boost::make_shared<LibraryTempFilePair>();
  compile_library(err_loc, result->path.path(), source);
  try {
    result->library = Platform::load_library(result->path.path());
  } catch (Platform::PlatformError& ex) {
    err_loc.error_throw(ex.what());
  }
  return boost::shared_ptr<Platform::PlatformLibrary>(result, result->library.get());
}

/**
 * \brief Emit an unreachable statement.
 * 
 * \return True if such a statement is supported (and thus emitted successfully).
 */
bool CCompiler::emit_unreachable(CModuleEmitter& PSI_UNUSED(emitter)) {
  return false;
}

struct CompilerCommonType {
  enum Mode {
    mode_int=0, /// Signed integer type
    mode_uint=1, /// Unsigned integer type
    mode_float=2 /// Float type
  };

  Mode mode;
  std::string name;
  /// Numeric suffix
  std::string suffix;
  unsigned size;
  unsigned alignment;
};

struct CompilerCommonInfo {
  bool big_endian;
  unsigned char_bits;
  unsigned pointer_size;
  unsigned pointer_alignment;
  std::vector<CompilerCommonType> types;
};

namespace {
  /// Return a copy of s with the whitespace at either end removed
  std::string trim(const std::string& s) {
    const std::locale& loc = std::locale::classic();
    std::size_t start, end;
    std::size_t n = s.size();
    for (start = 0; (start != n) && std::isspace(s[start], loc); ++start) {}
    if (start == n)
      return "";
    for (end = n; (end != 0) && std::isspace(s[end-1], loc); --end) {}
    PSI_ASSERT(start < end);
    return s.substr(start, end);
  }
}

/**
 * Common implementation for CCompiler.
 * 
 * This turns the type/alignment problem into a table.
 */
class CCompilerCommon : public CCompiler {
  bool m_big_endian;
  
public:
  CCompilerCommon(const CompilerCommonInfo& common_info) {
    m_big_endian = common_info.big_endian;
    primitive_types.pointer_size = common_info.pointer_size;
    primitive_types.pointer_alignment = common_info.pointer_alignment;
    
    for (std::vector<CompilerCommonType>::const_iterator ii = common_info.types.begin(), ie = common_info.types.end(); ii != ie; ++ii) {
      PrimitiveType si;
      if (ii->suffix == "-")
        si.suffix = "";
      else if (ii->suffix != ".")
        si.suffix = ii->suffix;
      si.name = ii->name;
      si.size = ii->size;
      si.alignment = ii->alignment;
      
      unsigned bits = ii->size * common_info.char_bits;
      switch (ii->mode) {
      case CompilerCommonType::mode_int:
      case CompilerCommonType::mode_uint: {
        if ((si.size == common_info.pointer_size) && (si.alignment == common_info.pointer_alignment)) {
          PrimitiveType& ptr_slot = (ii->mode == CompilerCommonType::mode_int ? primitive_types.int_types : primitive_types.uint_types)[IntegerType::iptr];
          if (ptr_slot.name.empty())
            ptr_slot = si;
        }
        
        IntegerType::Width w;
        if (bits >= 128) w = IntegerType::i128;
        else if (bits >= 64) w = IntegerType::i64;
        else if (bits >= 32) w = IntegerType::i32;
        else if (bits >= 16) w = IntegerType::i16;
        else if (bits >= 8) w = IntegerType::i8;
        else
          break;
        
        PrimitiveType& slot = (ii->mode == CompilerCommonType::mode_int ? primitive_types.int_types : primitive_types.uint_types)[w];
        if (slot.name.empty() || (si.size < slot.size))
          slot = si;
        
        break;
      }
      
      case CompilerCommonType::mode_float: {
        FloatType::Width w;
        if (bits >= 64) w = FloatType::fp64;
        else if (bits >= 32) w = FloatType::fp32;
        else
          break;
        
        PrimitiveType& slot = primitive_types.float_types[w];
        if (slot.name.empty() || (si.size < slot.size))
          slot = si;
        
        break;
      }
      
      default: PSI_FAIL("Unrecognied C type mode");
      }
    }
  }
  
  /**
   * \brief Parse compiler types.
   * 
   * The first line is expected to be
   * <pre>big_endian ptr_size ptr_alignment</pre>
   * 
   * All remaining lines in \c in are expected to have the format
   * <pre>size alignment name</pre>
   * All bytes after the size and alignment are interpreted as the name,
   * after trimming whitespace.
   */
  static CompilerCommonInfo parse_common_info(const CompileErrorPair& err_loc, std::istream& in) {
    CompilerCommonInfo ci;
    in >> ci.big_endian >> ci.char_bits >> ci.pointer_size >> ci.pointer_alignment;
    if (!in)
      err_loc.error_throw("Failed to parse C compiler common information");
    
    while (true) {
      CompilerCommonType type;
      int mode;
      in >> mode >> type.suffix >> type.size >> type.alignment;
      if (in.eof())
        return ci;
      else if (!in.good())
        err_loc.error_throw("Failed to parse C compiler common information");
      
      type.mode = static_cast<CompilerCommonType::Mode>(mode);
      std::getline(in, type.name);
      type.name = trim(type.name);
      ci.types.push_back(type);
    }
  }
};

namespace {
struct CommonTypeName {
  CompilerCommonType::Mode mode;
  const char *name;
  const char *suffix;
};

/**
  * Table of types supported by most C compilers.
  */
const CommonTypeName common_types[] = {
  {CompilerCommonType::mode_int, "signed char", "-"},
  {CompilerCommonType::mode_uint, "unsigned char", "-"},
  {CompilerCommonType::mode_int, "short", "-"},
  {CompilerCommonType::mode_uint, "unsigned short", "-"},
  {CompilerCommonType::mode_int, "int", "-"},
  {CompilerCommonType::mode_uint, "unsigned int", "-"},
  {CompilerCommonType::mode_int, "long", "L"},
  {CompilerCommonType::mode_uint, "unsigned long", "UL"},
  {CompilerCommonType::mode_int, "long long", "LL"},
  {CompilerCommonType::mode_uint, "unsigned long long", "ULL"}
};

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

class CCompilerMSVC : public CCompilerCommon {
  Platform::Path m_path;
  unsigned m_version;

public:
  CCompilerMSVC(const CompilerCommonInfo& common_info, const Platform::Path& path, unsigned version)
  : CCompilerCommon(common_info), m_path(path), m_version(version) {
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

  static void run_msvc_common(const CompileErrorPair& err_loc, const Platform::Path& path,
                              const Platform::Path& output_file, const std::string& source,
                              const std::vector<std::string>& extra) {
    Platform::TemporaryPath source_path;
    std::filebuf source_file;
    source_file.open(source_path.path().str().c_str(), std::ios::out);
    std::copy(source.begin(), source.end(), std::ostreambuf_iterator<char>(&source_file));
    source_file.close();

    std::vector<std::string> command;
    command.insert(command.end(), extra.begin(), extra.end());
    command.push_back("/Tc");
    command.push_back(source_path.path().str());
    command.push_back("/Fe");
    command.push_back(output_file.str());
    try {
      Platform::exec_communicate_check(path, command);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
  }
  
  static void run_msvc_program(const CompileErrorPair& err_loc, const Platform::Path& path,
                               const Platform::Path& output_file, const std::string& source) {
    std::vector<std::string> extra;
#ifdef _DEBUG
    extra.push_back("/MTd");
#else
    extra.push_back("/MT");
#endif
    run_msvc_common(err_loc, path, output_file, source, extra);
  }
  
  static void run_msvc_library(const CompileErrorPair& err_loc, const Platform::Path& path,
                               const Platform::Path& output_file, const std::string& source) {
    std::vector<std::string> extra;
#ifdef _DEBUG
    extra.push_back("/MDd");
#else
    extra.push_back("/MD");
#endif
    run_msvc_common(err_loc, path, output_file, source, extra);
  }

  virtual void compile_program(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_msvc_program(err_loc, m_path, output_file, source);
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_msvc_library(err_loc, m_path, output_file, source);
  }

  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const Platform::Path& path, const PropertyValue&) {
    std::ostringstream src;
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "int main() {\n"
        << "  union {unsigned __int8 a[4]; unsigned __int32 b;} endian_test = {1, 2, 3, 4};\n"
        << "  int big_endian = (endian_test.b == 0x01020304), little_endian = (endian_test.b == 0x04030201);\n"
        << "  printf(\"%d %d\\n\", _MSC_VER, big_endian||little_endian);\n"
        << "  printf(\"%d %d %d %d\\n\", big_endian, CHAR_BIT, (int)sizeof(void*), (int)__alignof(void*));\n";
    for (unsigned n = 0; n < array_size(common_types); ++n) {
      const CommonTypeName& ty = common_types[n];
      src << "  printf(\"" << ty.mode << " " << ty.suffix << " %d %d "
          << ty.name << "\\n\", (int)sizeof(" << ty.name
          << "), (int)__alignof(" << ty.name << "));\n";
    }
    src << "  return 0;"
        << "}\n";
    
    Platform::TemporaryPath program_path;
    run_msvc_program(err_loc, path, program_path.path(), src.str());
    
    std::string program_output;
    try {
      Platform::exec_communicate_check(program_path.path(), "", &program_output);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
    
    std::istringstream program_ss;
    program_ss.imbue(std::locale::classic());
    program_ss.str(program_output);
    unsigned version, known_endian;
    program_ss >> version >> known_endian;
    
    if (!known_endian)
      err_loc.error_throw("Microsoft C compiler uses unsupported byte order");
    
    CompilerCommonInfo common_info = CCompilerCommon::parse_common_info(err_loc, program_ss);
    
    return boost::make_shared<CCompilerMSVC>(common_info, path, version);
  }
};

/**
 * Base class for compilers which implement GCC \c __attribute__ extension.
 */
class CCompilerGCCLike : public CCompilerCommon {
public:
  CCompilerGCCLike(const CompilerCommonInfo& common_info)
  : CCompilerCommon(common_info) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }

  virtual void emit_alignment(CModuleEmitter& emitter, unsigned n) {
    emitter.output() << "__attribute__((aligned(" << n << "))) ";
  }

  void emit_global_attributes(AttributeWriter& aw, CGlobal *global, bool is_external) {
    if (global->alignment) aw.next() << "aligned(" << global->alignment << ")";
#ifdef _WIN32
    if (is_external) aw.next() << "dllimport";
    else if (!global->is_private) aw.next() << "dllexport";
#endif
  }
  
  /// \todo Emit calling convention
  virtual void emit_function_attributes(CModuleEmitter& emitter, CFunction *function) {
    AttributeWriter aw(emitter, "__attribute__((", "))");
    emit_global_attributes(aw, function, function->is_external);
    aw.done();
  }
  
  virtual void emit_global_variable_attributes(CModuleEmitter& emitter, CGlobalVariable *gvar) {
    AttributeWriter aw(emitter, "__attribute__((", "))");
    emit_global_attributes(aw, gvar, !gvar->value);
    aw.done();
  }
  
  static void run_gcc_common(const CompileErrorPair& err_loc, const Platform::Path& path,
                             const Platform::Path& output_file, const std::string& source,
                             const std::vector<std::string>& extra) {
    std::vector<std::string> command;
    command.push_back("-xc"); // Required because we pipe source to GCC
    command.push_back("-std=c99");
    command.insert(command.end(), extra.begin(), extra.end());
    command.push_back("-");
    command.push_back("-o");
    command.push_back(output_file.str());
    try {
      Platform::exec_communicate_check(path, command, source);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
  }
  
  static void run_gcc_program(const CompileErrorPair& err_loc, const Platform::Path& path,
                              const Platform::Path& output_file, const std::string& source) {
    run_gcc_common(err_loc, path, output_file, source, std::vector<std::string>());
  }
  
  static void run_gcc_library(const CompileErrorPair& err_loc, const Platform::Path& path,
                              const Platform::Path& output_file, const std::string& source) {
    std::vector<std::string> extra;
    extra.push_back("-shared");
#ifdef __linux__
    extra.push_back("-fPIC");
    extra.push_back("-Wl,-soname," + Platform::filename(output_file));
#endif
    run_gcc_common(err_loc, path, output_file, source, extra);
  }

  static void gcc_type_detection_code(std::ostream& src, const char *fp="stdout") {
    for (unsigned n = 0; n < array_size(common_types); ++n) {
      const CommonTypeName& ty = common_types[n];
      src << "  fprintf(" << fp << ", \"" << ty.mode << " " << ty.suffix << " %d %d "
          << ty.name << "\\n\", (int)sizeof(" << ty.name
          << "), (int)__alignof__(" << ty.name << "));\n";
    }
  }
};

class CCompilerGCC : public CCompilerGCCLike {
  Platform::Path m_path;
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerGCC(const CompilerCommonInfo& common_info, const Platform::Path& path, unsigned major, unsigned minor)
  : CCompilerGCCLike(common_info), m_path(path), m_major_version(major), m_minor_version(minor) {
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
  
  virtual void compile_program(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_gcc_program(err_loc, m_path, output_file, source);
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_gcc_library(err_loc, m_path, output_file, source);
  }
  
  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const Platform::Path& path, const PropertyValue&) {
    std::ostringstream src;
    src.imbue(std::locale::classic());
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "int main() {\n"
        << "  int big_endian = (__BYTE_ORDER__==__ORDER_BIG_ENDIAN__), little_endian = (__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__);\n"
        << "  printf(\"%d %d %d\\n\", __GNUC__, __GNUC_MINOR__, big_endian||little_endian);\n"
        << "  printf(\"%d %d %d %d\\n\", big_endian, CHAR_BIT, (int)sizeof(void*), (int)__alignof__(void*));\n";
    gcc_type_detection_code(src);
    src << "  return 0;"
        << "}\n";
    
    Platform::TemporaryPath program_path;
    run_gcc_program(err_loc, path, program_path.path(), src.str());
    
    std::string program_output;
    try {
      Platform::exec_communicate_check(program_path.path(), "", &program_output);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
    
    std::istringstream program_ss;
    program_ss.imbue(std::locale::classic());
    program_ss.str(program_output);
    unsigned version_major, version_minor, known_endian;
    program_ss >> version_major >> version_minor >> known_endian;
    
    if (!known_endian)
      err_loc.error_throw("GCC compiler uses unsupported byte order");
    
    CompilerCommonInfo common_info = CCompilerCommon::parse_common_info(err_loc, program_ss);
    
    return boost::make_shared<CCompilerGCC>(common_info, path, version_major, version_minor);
  }
};

class CCompilerClang : public CCompilerGCCLike {
  Platform::Path m_path;
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerClang(const CompilerCommonInfo& common_info, const Platform::Path& path, unsigned major, unsigned minor)
  : CCompilerGCCLike(common_info), m_path(path), m_major_version(major), m_minor_version(minor) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }

  virtual bool emit_unreachable(CModuleEmitter& emitter) {
    emitter.output() << "__builtin_unreachable()";
    return true;
  }
  
  virtual void compile_program(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_gcc_program(err_loc, m_path, output_file, source);
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_gcc_library(err_loc, m_path, output_file, source);
  }

  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const Platform::Path& path, const PropertyValue&) {
    std::ostringstream src;
    src.imbue(std::locale::classic());
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "#include <stdint.h>\n"
        << "int main() {\n"
        << "  union {uint8_t a[4]; uint32_t b;} endian_test = {1, 2, 3, 4};\n"
        << "  int big_endian = (endian_test.b == 0x01020304), little_endian = (endian_test.b == 0x04030201);\n"
        << "  printf(\"%d %d %d\\n\", __clang_major__, __clang_minor__, big_endian||little_endian);\n"
        << "  printf(\"%d %d %d %d\\n\", big_endian, CHAR_BIT, (int)sizeof(void*), (int)__alignof__(void*));\n";
    gcc_type_detection_code(src);
    src << "  return 0;"
        << "}\n";
    
    Platform::TemporaryPath program_path;
    run_gcc_program(err_loc, path, program_path.path(), src.str());
    
    std::string program_output;
    try {
      Platform::exec_communicate_check(program_path.path(), "", &program_output);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
    
    std::istringstream program_ss;
    program_ss.imbue(std::locale::classic());
    program_ss.str(program_output);
    unsigned version_major, version_minor, known_endian;
    program_ss >> version_major >> version_minor >> known_endian;
    
    if (!known_endian)
      err_loc.error_throw("clang compiler uses unsupported byte order");
    
    CompilerCommonInfo common_info = CCompilerCommon::parse_common_info(err_loc, program_ss);
    
    return boost::make_shared<CCompilerClang>(common_info, path, version_major, version_minor);
  }
};

class CCompilerTCC : public CCompilerGCCLike {
  Platform::Path m_path;
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerTCC(const CompilerCommonInfo& common_info, const Platform::Path& path, unsigned major, unsigned minor)
  : CCompilerGCCLike(common_info), m_path(path), m_major_version(major), m_minor_version(minor) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }
  
  static void run_tcc_common(const CompileErrorPair& err_loc, const Platform::Path& path,
                             const Platform::Path& output_file, const std::string& source,
                             const std::vector<std::string>& extra) {
    Platform::TemporaryPath source_path;
    std::filebuf source_file;
    source_file.open(source_path.path().str().c_str(), std::ios::out);
    std::copy(source.begin(), source.end(), std::ostreambuf_iterator<char>(&source_file));
    source_file.close();

    std::vector<std::string> command;
    command.insert(command.end(), extra.begin(), extra.end());
    command.push_back(source_path.path().str());
    command.push_back("-g");
    command.push_back("-o");
    command.push_back(output_file.str());
    try {
      Platform::exec_communicate_check(path, command, source);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
  }
  
  virtual void compile_program(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    run_tcc_common(err_loc, m_path, output_file, source, std::vector<std::string>());
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    std::vector<std::string> extra;
    extra.push_back("-shared");
    extra.push_back("-soname");
    extra.push_back(output_file.filename().str());
    run_tcc_common(err_loc, m_path, output_file, source, extra);
  }

  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const Platform::Path& path, const PropertyValue&) {
    std::stringstream src;
    src.imbue(std::locale::classic());
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "#include <stdint.h>\n"
        << "int main() {\n"
        << "  union {uint8_t a[4]; uint32_t b;} endian_test = {1, 2, 3, 4};\n"
        << "  int big_endian = (endian_test.b == 0x01020304), little_endian = (endian_test.b == 0x04030201);\n"
        << "  printf(\"%d %d\\n\", __TINYC__, big_endian||little_endian);\n"
        << "  printf(\"%d %d %d %d\\n\", big_endian, CHAR_BIT, (int)sizeof(void*), (int)__alignof__(void*));\n";
    gcc_type_detection_code(src);
    src << "  return 0;"
        << "}\n";
    
    Platform::TemporaryPath source_path;
    std::filebuf source_file;
    source_file.open(source_path.path().str().c_str(), std::ios::out);
    std::copy(std::istreambuf_iterator<char>(src.rdbuf()),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(&source_file));
    source_file.close();
    
    std::vector<std::string> tcc_args;
    tcc_args.push_back("-xc");
    tcc_args.push_back("-run");
    tcc_args.push_back(source_path.path().str());
    
    std::string program_output;
    try {
      Platform::exec_communicate_check(path, tcc_args, "", &program_output);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
    
    std::istringstream program_ss;
    program_ss.imbue(std::locale::classic());
    program_ss.str(program_output);
    unsigned version, known_endian;
    program_ss >> version >> known_endian;
    
    if (!known_endian)
      err_loc.error_throw("tcc compiler uses unsupported byte order");
    
    unsigned version_major, version_minor;
    version_major = version / 10000;
    version_minor = (version / 100) % 100;
    
    CompilerCommonInfo common_info = CCompilerCommon::parse_common_info(err_loc, program_ss);
    
    return boost::make_shared<CCompilerTCC>(common_info, path, version_major, version_minor);
  }
};

namespace {
class TCCError : public std::exception {
  std::string m_msg;
public:
  TCCError(const char *s) : m_msg(s) {}
  TCCError(std::string& s) {m_msg.swap(s);}
  virtual ~TCCError() throw() {}
  virtual const char *what() const throw() {return m_msg.c_str();}
};

struct TCCConfiguration {
  boost::optional<std::string> path;
  boost::optional<std::string> include_path;
};

class TCCLibContext {
  std::string m_error_msg;
  TCCState *m_state;

  static void error_func(void *self_void_ptr, const char *msg) {
    TCCLibContext& self = *static_cast<TCCLibContext*>(self_void_ptr);
    self.m_error_msg = msg;
  }

  void clear_tcc_error() {
    m_error_msg.clear();
  }

  void throw_tcc_error() {
    if (m_error_msg.empty())
      throw TCCError("Unknown TCC error");
    throw TCCError(m_error_msg);
  }

public:
  TCCLibContext(const TCCConfiguration& config, int output_type) {
    m_state = tcc_new();
    if (!m_state)
      throw TCCError("Failed to create TCC context");

    if (config.include_path)
      tcc_add_sysinclude_path(m_state, config.include_path->c_str());
    if (config.path)
      tcc_set_lib_path(m_state, config.path->c_str());

    tcc_set_error_func(m_state, this, error_func);
    if (tcc_set_output_type(m_state, output_type) == -1)
      throw_tcc_error();
  }

  ~TCCLibContext() {
    tcc_delete(m_state);
  }

  TCCState *state() {
    return m_state;
  }

  void compile_string(const std::string& src) {
    clear_tcc_error();
    if (tcc_compile_string(m_state, src.c_str()) == -1)
      throw_tcc_error();
  }

  void output_file(const std::string& name) {
    clear_tcc_error();
    if (tcc_output_file(m_state, name.c_str()) == -1)
      throw_tcc_error();
  }

  int run(int argc, char **argv) {
    return tcc_run(m_state, argc, argv);
  }

  void relocate() {
    clear_tcc_error();
    if (tcc_relocate(m_state, TCC_RELOCATE_AUTO) == -1)
      throw_tcc_error();
  }

  void add_symbol(const char *name, const void *value) {
    clear_tcc_error();
    if (tcc_add_symbol(m_state, name, value) == -1)
      throw_tcc_error();
  }

  void* get_symbol(const std::string& name) {
    return tcc_get_symbol(m_state, name.c_str());
  }
};

/**
 * Implementation of PlatformLibrary for TCC memory compiled modules.
 */
class TCCPlatformLibrary : public Platform::PlatformLibrary {
  TCCLibContext m_context;

public:
  TCCPlatformLibrary(const TCCConfiguration& config) : m_context(config, TCC_OUTPUT_MEMORY) {}
  TCCLibContext& context() {return m_context;}

  virtual boost::optional<void*> symbol(const std::string& name) {
    void *s = m_context.get_symbol(name);
    return s ? boost::optional<void*>(s) : boost::none;
  }
};

class StdioTempFile : public NonCopyable {
  FILE *m_fp;
public:
  StdioTempFile() {
    m_fp = tmpfile();
  }

  ~StdioTempFile() {
    if (m_fp)
      fclose(m_fp);
  }

  FILE *fp() {return m_fp;}
};
}

class CCompilerTCCLib : public CCompilerGCCLike {
  unsigned m_version_major, m_version_minor;
  TCCConfiguration m_configuration;

public:
  CCompilerTCCLib(const CompilerCommonInfo& info, const TCCConfiguration& configuration, unsigned version_major, unsigned version_minor)
  : CCompilerGCCLike(info), m_configuration(configuration), m_version_major(version_major), m_version_minor(version_minor) {
  }

  virtual void compile_program(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    try {
      TCCLibContext ctx(m_configuration, TCC_OUTPUT_EXE);
      ctx.compile_string(source);
      ctx.output_file(output_file.str());
    } catch (TCCError& ex) {
      err_loc.error_throw(ex.what());
    }
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const Platform::Path& output_file, const std::string& source) {
    try {
      TCCLibContext ctx(m_configuration, TCC_OUTPUT_DLL);
      ctx.compile_string(source);
      ctx.output_file(output_file.str());
    } catch (TCCError& ex) {
      err_loc.error_throw(ex.what());
    }
  }

  virtual boost::shared_ptr<Platform::PlatformLibrary> compile_load_library(const CompileErrorPair& err_loc, const std::string& source) {
    try {
      boost::shared_ptr<TCCPlatformLibrary> pl = boost::make_shared<TCCPlatformLibrary>(m_configuration);
      pl->context().compile_string(source);
      pl->context().relocate();
      return pl;
    } catch (TCCError& ex) {
      err_loc.error_throw(ex.what());
    }
  }

  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const PropertyValue& config) {
    TCCConfiguration tcc_config;
    tcc_config.include_path = config.path_str("include");
    tcc_config.path = config.path_str("path");

    std::stringstream src;
    src.imbue(std::locale::classic());
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "#include <stdint.h>\n"
        << "void callback(FILE *fp) {\n"
        << "  union {uint8_t a[4]; uint32_t b;} endian_test = {1, 2, 3, 4};\n"
        << "  int big_endian = (endian_test.b == 0x01020304), little_endian = (endian_test.b == 0x04030201);\n"
        << "  fprintf(fp, \"%d %d\\n\", __TINYC__, big_endian||little_endian);\n"
        << "  fprintf(fp, \"%d %d %d %d\\n\", big_endian, CHAR_BIT, (int)sizeof(void*), (int)__alignof__(void*));\n";
    CCompilerGCCLike::gcc_type_detection_code(src, "fp");
    src << "}\n";

    std::vector<char> output;
    try {
      boost::shared_ptr<TCCPlatformLibrary> pl = boost::make_shared<TCCPlatformLibrary>(tcc_config);
      pl->context().compile_string(src.str());
      pl->context().add_symbol("fprintf", fprintf);
      pl->context().relocate();

      void (*fptr) (FILE*) = reinterpret_cast<void(*)(FILE*)>(pl->context().get_symbol("callback"));

      StdioTempFile tf;
      if (!tf.fp())
        err_loc.error_throw("Failed to create temporary file");
      fptr(tf.fp());
      
      long length = ftell(tf.fp());
      if (length < 0)
        PSI_NOT_IMPLEMENTED();

      output.resize(length);
      if (length > 0) {
        if (fseek(tf.fp(), 0, SEEK_SET) != 0)
          PSI_NOT_IMPLEMENTED();

        if (fread(&output[0], 1, length, tf.fp()) != length)
          PSI_NOT_IMPLEMENTED();
      }
    } catch (TCCError& ex) {
      err_loc.error_throw(ex.what());
    }

    std::istringstream program_ss;
    program_ss.imbue(std::locale::classic());
    program_ss.str(std::string(output.begin(), output.end()));
    unsigned version, known_endian;
    program_ss >> version >> known_endian;
    
    if (!known_endian)
      err_loc.error_throw("tcc compiler uses unsupported byte order");
    
    unsigned version_major, version_minor;
    version_major = version / 10000;
    version_minor = (version / 100) % 100;
    
    CompilerCommonInfo common_info = CCompilerCommon::parse_common_info(err_loc, program_ss);
    
    return boost::make_shared<CCompilerTCCLib>(common_info, tcc_config, version_major, version_minor);
  }
};

/**
 * Try to locate a C compiler on the system.
 */
boost::shared_ptr<CCompiler> detect_c_compiler(const CompileErrorPair& err_loc, const PropertyValue& configuration) {
  String key = configuration.get("cc").str();
  const PropertyValue& cc_config = configuration.get(key);

  String kind = cc_config.get("kind").str();
  if (kind == "tcclib") {
    return CCompilerTCCLib::detect(err_loc, cc_config);
  } else {
    // Try to identify the compiler by its executable name
    Platform::Path cc_path = std::string(cc_config.get("path").str());
    boost::optional<Platform::Path> cc_full_path = Platform::find_in_path(cc_path);
    if (!cc_full_path)
      err_loc.error_throw(boost::format("C compiler not found: %s") % cc_path.str());

    if (kind == "gcc") {
      return CCompilerGCC::detect(err_loc, *cc_full_path, cc_config);
    } else if (kind == "clang") {
      return CCompilerClang::detect(err_loc, *cc_full_path, cc_config);
    } else if (kind == "tcc") {
      return CCompilerTCC::detect(err_loc, *cc_full_path, cc_config);
    } else {
      err_loc.error_throw(boost::format("Could not identify C compiler: %s") % cc_full_path->str());
    }
  }
}
}
}
}
