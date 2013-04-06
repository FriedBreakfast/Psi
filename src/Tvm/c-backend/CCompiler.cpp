#include "Builder.hpp"
#include "CModule.hpp"
#include "../../Platform.hpp"

#include <fstream>
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
    int big_endian;
    in >> big_endian >> ci.char_bits >> ci.pointer_size >> ci.pointer_alignment;
    ci.big_endian = big_endian;
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
class CCompilerGCCLike : public CCompilerCommon {
public:
  bool has_float_128;
  bool has_float_80;
  
  CCompilerGCCLike(const CompilerCommonInfo& common_info)
  : CCompilerCommon(common_info),
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
  
  virtual const char *int_suffix(CModule&, IntegerType::Width width, bool is_signed) {
    IntegerType::Width my_width = width;
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
  
  static void run_gcc_common(const CompileErrorPair& err_loc, const std::string& path,
                             const std::string& output_file, const std::string& source,
                             const std::vector<std::string>& extra) {
    std::vector<std::string> command;
    command.push_back(path);
    command.push_back("-xc"); // Required because we pipe source to GCC
    command.push_back("-std=c99");
    command.insert(command.end(), extra.begin(), extra.end());
    command.push_back("-");
    command.push_back("-o");
    command.push_back(output_file);
    try {
      Platform::exec_communicate_check(command, source);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
  }
  
  static void run_gcc_program(const CompileErrorPair& err_loc, const std::string& path,
                              const std::string& output_file, const std::string& source) {
    run_gcc_common(err_loc, path, output_file, source, std::vector<std::string>());
  }
  
  static void run_gcc_library_linux(const CompileErrorPair& err_loc, const std::string& path,
                                    const std::string& output_file, const std::string& source) {
    std::vector<std::string> extra;
    extra.push_back("-shared");
    extra.push_back("-fPIC");
    extra.push_back("-Wl,-soname," + Platform::filename(output_file));
    run_gcc_common(err_loc, path, output_file, source, extra);
  }

  static void gcc_type_detection_code(std::ostream& src) {
    const struct {CompilerCommonType::Mode mode; const char *name; const char *suffix;} gcc_types[] = {
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
    for (unsigned n = 0; n < 10; ++n) {
      src << "  printf(\"" << gcc_types[n].mode << " " << gcc_types[n].suffix << " %zd %zd "
          << gcc_types[n].name << "\\n\", sizeof(" << gcc_types[n].name
          << "), __alignof__(" << gcc_types[n].name << "));\n";
    }
  }
};

class CCompilerGCC : public CCompilerGCCLike {
  std::string m_path;
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerGCC(const CompilerCommonInfo& common_info, const std::string& path, unsigned major, unsigned minor)
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
  
  virtual void compile_program(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) {
    run_gcc_program(err_loc, m_path, output_file, source);
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) {
    run_gcc_library_linux(err_loc, m_path, output_file, source);
  }
  
  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const std::string& path) {
    std::ostringstream src;
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "int main() {\n"
        << "  int big_endian = (__BYTE_ORDER__==__ORDER_BIG_ENDIAN__), little_endian = (__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__);\n"
        << "  printf(\"%d %d %d\\n\", __GNUC__, __GNUC_MINOR__, big_endian||little_endian);\n"
        << "  printf(\"%d %d %zd %zd\\n\", big_endian, CHAR_BIT, sizeof(void*), __alignof__(void*));\n";
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
  std::string m_path;
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerClang(const CompilerCommonInfo& common_info, const std::string& path, unsigned major, unsigned minor)
  : CCompilerGCCLike(common_info), m_path(path), m_major_version(major), m_minor_version(minor) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }

  virtual bool emit_unreachable(CModuleEmitter& emitter) {
    emitter.output() << "__builtin_unreachable()";
    return true;
  }
  
  virtual void compile_program(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) {
    run_gcc_program(err_loc, m_path, output_file, source);
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) {
    run_gcc_library_linux(err_loc, m_path, output_file, source);
  }

  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const std::string& path) {
    std::ostringstream src;
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "#include <stdint.h>\n"
        << "int main() {\n"
        << "  union {uint8_t a[4]; uint32_t b;} endian_test = {1, 2, 3, 4};\n"
        << "  int big_endian = (endian_test.b == 0x01020304), little_endian = (endian_test.b == 0x04030201);\n"
        << "  printf(\"%d %d %d\\n\", __clang_major__, __clang_minor__, big_endian||little_endian);\n"
        << "  printf(\"%d %d %zd %zd\\n\", big_endian, CHAR_BIT, sizeof(void*), __alignof__(void*));\n";
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
  std::string m_path;
  unsigned m_major_version, m_minor_version;
  
public:
  CCompilerTCC(const CompilerCommonInfo& common_info, const std::string& path, unsigned major, unsigned minor)
  : CCompilerGCCLike(common_info), m_path(path), m_major_version(major), m_minor_version(minor) {
    has_variable_length_arrays = true;
    has_designated_initializer = true;
  }
  
  static void run_tcc_common(const CompileErrorPair& err_loc, const std::string& path,
                             const std::string& output_file, const std::string& source,
                             const std::vector<std::string>& extra) {
    Platform::TemporaryPath source_path;
    std::filebuf source_file;
    source_file.open(source_path.path().c_str(), std::ios::out);
    std::copy(source.begin(), source.end(), std::ostreambuf_iterator<char>(&source_file));
    source_file.close();

    std::vector<std::string> command;
    command.push_back(path);
    command.insert(command.end(), extra.begin(), extra.end());
    command.push_back(source_path.path());
    command.push_back("-g");
    command.push_back("-o");
    command.push_back(output_file);
    try {
      Platform::exec_communicate_check(command, source);
    } catch (Platform::PlatformError& ex) {
      err_loc.error_throw(ex.what());
    }
  }
  
  virtual void compile_program(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) {
    run_tcc_common(err_loc, m_path, output_file, source, std::vector<std::string>());
  }
  
  virtual void compile_library(const CompileErrorPair& err_loc, const std::string& output_file, const std::string& source) {
    std::vector<std::string> extra;
    extra.push_back("-shared");
    extra.push_back("-soname");
    extra.push_back(Platform::filename(output_file));
    run_tcc_common(err_loc, m_path, output_file, source, extra);
  }

  static boost::shared_ptr<CCompiler> detect(const CompileErrorPair& err_loc, const std::string& path) {
    std::stringstream src;
    src << "#include <stdio.h>\n"
        << "#include <limits.h>\n"
        << "#include <stdint.h>\n"
        << "int main() {\n"
        << "  union {uint8_t a[4]; uint32_t b;} endian_test = {1, 2, 3, 4};\n"
        << "  int big_endian = (endian_test.b == 0x01020304), little_endian = (endian_test.b == 0x04030201);\n"
        << "  printf(\"%d %d\\n\", __TINYC__, big_endian||little_endian);\n"
        << "  printf(\"%d %d %zd %zd\\n\", big_endian, CHAR_BIT, sizeof(void*), __alignof__(void*));\n";
    gcc_type_detection_code(src);
    src << "  return 0;"
        << "}\n";
    
    Platform::TemporaryPath source_path;
    std::filebuf source_file;
    source_file.open(source_path.path().c_str(), std::ios::out);
    std::copy(std::istreambuf_iterator<char>(src.rdbuf()),
              std::istreambuf_iterator<char>(),
              std::ostreambuf_iterator<char>(&source_file));
    source_file.close();
    
    std::vector<std::string> tcc_args;
    tcc_args.push_back(path);
    tcc_args.push_back("-xc");
    tcc_args.push_back("-run");
    tcc_args.push_back(source_path.path());
    
    std::string program_output;
    try {
      Platform::exec_communicate_check(tcc_args, "", &program_output);
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
boost::shared_ptr<CCompiler> detect_c_compiler(const CompileErrorPair& err_loc) {
  const char *cc_path = std::getenv("PSI_TVM_CC");
  if (!cc_path)
    cc_path = PSI_TVM_CC;
  
  if (PSI_TVM_CC_TCCLIB && (std::strcmp(cc_path, "tcclib") == 0))
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
  if (!result && ((type == cc_unknown) || (type == cc_clang)))
    result = CCompilerClang::detect(err_loc, *cc_full_path);
  if (!result && ((type == cc_unknown) || (type == cc_tcc)))
    result = CCompilerTCC::detect(err_loc, *cc_full_path);
  if (!result && ((type == cc_unknown) || (type == cc_msvc))) PSI_NOT_IMPLEMENTED();
  
  if (!result)
    err_loc.error_throw(boost::format("Could not identify C compiler: %s") % *cc_full_path);
  
  return result;
}
}
}
}
