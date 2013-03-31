#include "ErrorContext.hpp"

#include <boost/format.hpp>

namespace Psi {
CompileErrorContext::CompileErrorContext(std::ostream *error_stream)
: m_error_stream(error_stream),
m_error_occurred(false) {
}

void CompileErrorContext::error(const SourceLocation& loc, const std::string& message, unsigned flags) {
  CompileError error(*this, loc, flags);
  error.info(message);
  error.end();
}

void CompileErrorContext::error_throw(const SourceLocation& loc, const std::string& message, unsigned flags) {
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

  m_context->error_stream() << boost::format("%s:%s: in '%s'\n") % location.physical.file->url
    % location.physical.first_line % location.logical->error_name(LogicalSourceLocationPtr(), true);
}

void CompileError::info(const std::string& message) {
  info(m_location, message);
}

void CompileError::info(const SourceLocation& location, const std::string& message) {
  m_context->error_stream() << boost::format("%s:%s:%s: %s\n")
    % location.physical.file->url % location.physical.first_line % m_type % message;
}

void CompileError::end() {
}

CompileErrorPair::CompileErrorPair(CompileErrorContext& context, const SourceLocation& location)
: m_context(&context), m_location(location) {
}
}
