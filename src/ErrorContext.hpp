#ifndef HPP_PSI_ERROR_CONTEXT
#define HPP_PSI_ERROR_CONTEXT

#include "Export.hpp"
#include "SourceLocation.hpp"

#include <sstream>

/**
 * \file
 * 
 * Error handling functions
 */

namespace Psi {
class PSI_COMPILER_COMMON_EXPORT CompileException : public std::exception {
public:
  CompileException();
  virtual ~CompileException() throw();
  virtual const char *what() const throw();
};

class PSI_COMPILER_COMMON_EXPORT CompileErrorContext;

/**
 * \brief Class used for error reporting.
 */
class PSI_COMPILER_COMMON_EXPORT CompileError {
  CompileErrorContext *m_context;
  SourceLocation m_location;
  unsigned m_flags;
  const char *m_type;
  
public:
  enum ErrorFlags {
    error_warning=1,
    error_internal=2
  };

  template<typename T>
  static std::string to_str(const T& t) {
    std::ostringstream ss;
    ss << t;
    return ss.str();
  }
  
  CompileError(CompileErrorContext& context, const SourceLocation& location, unsigned flags=0);

  void info(const std::string& message);
  void info(const SourceLocation& location, const std::string& message);
  void end();

  const SourceLocation& location() {return m_location;}

  template<typename T> void info(const T& message) {info(to_str(message));}
  template<typename T> void info(const SourceLocation& location, const T& message) {info(location, to_str(message));}
};

class PSI_COMPILER_COMMON_EXPORT CompileErrorContext {
  std::ostream *m_error_stream;
  bool m_error_occurred;
  
public:
  CompileErrorContext(std::ostream *error_stream);

  /// \brief Return the stream used for error reporting.
  std::ostream& error_stream() {return *m_error_stream;}

  /// \brief Returns true if an error has occurred during compilation.
  bool error_occurred() const {return m_error_occurred;}
  /// \brief Call this to indicate an unrecoverable error occurred at some point during compilation.
  void set_error_occurred() {m_error_occurred = true;}

  void error(const SourceLocation& loc, const std::string& message, unsigned flags=0);
  PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const std::string& message, unsigned flags=0);
  template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, CompileError::to_str(message), flags);}
  template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, CompileError::to_str(message), flags);}
};

/**
 * \brief A combination of SourceLocation and CompileErrorContext, which lets low level classes only deal with one object rather than two.
 */
class PSI_COMPILER_COMMON_EXPORT CompileErrorPair {
  CompileErrorContext *m_context;
  SourceLocation m_location;
  
public:
  CompileErrorPair(CompileErrorContext& m_context, const SourceLocation& location);
  
  /// Forwards to CompileErrorContext::error
  void error(const std::string& message, unsigned flags=0) {m_context->error(m_location, message, flags);}
  /// Forwards to CompileErrorContext::error_throw
  PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const std::string& message, unsigned flags=0) {m_context->error(m_location, message, flags);}
  /// Forwards to CompileErrorContext::error
  template<typename T> void error(const T& message, unsigned flags=0) {m_context->error(m_location, message, flags);}
  /// Forwards to CompileErrorContext::error_throw
  template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const T& message, unsigned flags=0) {m_context->error_throw(m_location, message, flags);}
};
}

#endif
