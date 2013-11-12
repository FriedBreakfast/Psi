#include "ErrorContext.hpp"

#include <boost/format.hpp>

namespace Psi {
ErrorMessage::ErrorMessage(const char* s) : m_str(s) {
}

ErrorMessage::ErrorMessage(const std::string& s) : m_str(s) {
}

ErrorMessage::ErrorMessage(const boost::format& fmt) : m_str(fmt.str()) {
}

CompileErrorContext::CompileErrorContext(std::ostream *error_stream)
: m_error_stream(error_stream),
m_error_occurred(false) {
}

void CompileErrorContext::error(const SourceLocation& loc, const ErrorMessage& message, unsigned flags) {
  CompileError error(*this, loc, flags);
  error.info(message);
  error.end();
}

void CompileErrorContext::error_throw(const SourceLocation& loc, const ErrorMessage& message, unsigned flags) {
  error(loc, message, flags);
  throw CompileException();
}

CompileException::CompileException() {
}

CompileException::~CompileException() throw() {
}

const char *CompileException::what() const throw() {
  return "Psi compile exception";
}

CompileError::CompileError(CompileErrorContext& context, const SourceLocation& location, unsigned flags)
: m_context(&context), m_location(location), m_flags(flags) {
  bool error_occurred = false;
  switch (flags) {
  case error_warning: m_type = "warning"; break;
  case error_internal: m_type = "internal error"; error_occurred = true; break;
  default: m_type = "error"; error_occurred = true; break;
  }

  if (error_occurred)
    m_context->set_error_occurred();

  if (location.physical.file) {
    m_context->error_stream() << boost::format("%s:%s: in '%s'\n") % location.physical.file->url
      % location.physical.first_line % location.logical->error_name(LogicalSourceLocationPtr(), true);
  } else {
    m_context->error_stream() << boost::format("(no file):%s: in '%s'\n")
      % location.physical.first_line % location.logical->error_name(LogicalSourceLocationPtr(), true);
  }
}

void CompileError::info(const ErrorMessage& message) {
  info(m_location, message);
}

void CompileError::info(const SourceLocation& location, const ErrorMessage& message) {
  if (location.physical.file) {
    m_context->error_stream() << boost::format("%s:%s:%s: %s\n")
      % location.physical.file->url % location.physical.first_line % m_type % message.str();
  } else {
    m_context->error_stream() << boost::format("(no file):%s:%s: %s\n")
      % location.physical.first_line % m_type % message.str();
  }
}

void CompileError::end() {
}

void CompileError::end_throw() {
  end();
  throw CompileException();
}

CompileErrorPair::CompileErrorPair(CompileErrorContext& context, const SourceLocation& location)
: m_context(&context), m_location(location) {
}
}
