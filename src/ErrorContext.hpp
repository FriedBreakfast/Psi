#ifndef HPP_PSI_ERROR_CONTEXT
#define HPP_PSI_ERROR_CONTEXT

#include "Export.hpp"
#include "SourceLocation.hpp"

#include <sstream>
#include <boost/format/format_fwd.hpp>

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
 * A class which encapsulates error message formatting,
 * converting various different types to strings.
 */
class PSI_COMPILER_COMMON_EXPORT ErrorMessage {
  std::string m_str;
  
public:
  ErrorMessage(const char *s);
  ErrorMessage(const std::string& s);
  ErrorMessage(const boost::format& fmt);
  
  const std::string& str() const {return m_str;}
};

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
  
  CompileError(CompileErrorContext& context, const SourceLocation& location, unsigned flags=0);

  void info(const ErrorMessage& message);
  void info(const SourceLocation& location, const ErrorMessage& message);
  void end();
  PSI_ATTRIBUTE((PSI_NORETURN)) void end_throw();

  const SourceLocation& location() {return m_location;}
};

class CompileErrorPair;

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
  
  /// \brief Bind to a location to create a CompileErrorPair
  CompileErrorPair bind(const SourceLocation& location);

  void error(const SourceLocation& loc, const ErrorMessage& message, unsigned flags=0);
  PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const ErrorMessage& message, unsigned flags=0);
};

/**
 * \brief A combination of SourceLocation and CompileErrorContext, which lets low level classes only deal with one object rather than two.
 */
class PSI_COMPILER_COMMON_EXPORT CompileErrorPair {
  CompileErrorContext *m_context;
  SourceLocation m_location;
  
public:
  CompileErrorPair(CompileErrorContext& context, const SourceLocation& location);
  
  /// Forwards to CompileErrorContext::error
  void error(const ErrorMessage& message, unsigned flags=0) const {m_context->error(m_location, message, flags);}
  /// Forwards to CompileErrorContext::error_throw
  PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const ErrorMessage& message, unsigned flags=0) const {m_context->error_throw(m_location, message, flags);}
  
  /// \brief Get the underlying error context
  CompileErrorContext& context() const {return *m_context;}
  /// \brief Get the bound error reporting location
  const SourceLocation& location() const {return m_location;}
};

inline CompileErrorPair CompileErrorContext::bind(const SourceLocation& location) {return CompileErrorPair(*this, location);}
}

#endif
