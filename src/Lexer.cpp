#include "Lexer.hpp"

#include <cstring>

namespace Psi {
LexerPosition::LexerPosition(CompileErrorContext& error_context, const SourceLocation& loc, const char* start, const char* end)
: m_error_context(&error_context), m_error_location(loc.logical),
m_location(loc.physical), m_current(start), m_end(end), m_token_start(start) {
  m_location.last_column = m_location.first_column;
  m_location.last_line = m_location.first_line;
}

CompileErrorPair LexerPosition::error_loc(const PhysicalSourceLocation& loc) {
  return CompileErrorPair(*m_error_context, SourceLocation(loc, m_error_location));
}

void LexerPosition::error(const PhysicalSourceLocation& loc, const std::string& message) {
  error_loc(loc).error_throw(message);
}

/// \brief Accept the next character.
void LexerPosition::accept() {
  PSI_ASSERT(!end());
  if (*m_current == '\n') {
    ++m_location.last_line;
    m_location.last_column = 1;
  } else {
    ++m_location.last_column;
  }
  ++m_current;
}

/// \brief Set the start of the current token to the current position
void LexerPosition::begin() {
  m_location.first_line = m_location.last_line;
  m_location.first_column = m_location.last_column;
  m_token_start = m_current;
}

/// \brief Set the start of the current token to the current position
void LexerPosition::skip_whitespace() {
  while (!end() && std::strchr(" \t\r\n\v", current()))
    accept();
  begin();
}
}
