#ifndef HPP_PSI_LEXER
#define HPP_PSI_LEXER

#include <boost/format.hpp>

#include "ErrorContext.hpp"
#include "Utility.hpp"

namespace Psi {
class PSI_COMPILER_COMMON_EXPORT LexerPosition {
public:
  LexerPosition(CompileErrorContext& error_context, const SourceLocation& loc, const char *start, const char *end);
  ~LexerPosition();

  PSI_ATTRIBUTE((PSI_NORETURN)) void error(const PhysicalSourceLocation& loc, const ErrorMessage& message);
  CompileErrorPair error_loc(const PhysicalSourceLocation& loc);

  /// \brief Has the end of the character stream been reached
  bool end() const {return m_current == m_end;}
  /// \brief Return the character at the current stream position
  char current() const {PSI_ASSERT(!end()); return *m_current;}
  void accept();
  void begin();
  /// \brief Get the position of the token currently being generated
  const PhysicalSourceLocation& location() {return m_location;}
  
  void skip_whitespace();
  
  /// \brief Get a pointer to the start of the current token
  const char *token_start() {return m_token_start;}
  /// \brief Get a pointer to the end of the current token
  const char *token_end() {return m_current;}
  /// \brief Get the number of bytes in the current token.
  std::size_t token_length() const {return m_current - m_token_start;}

private:
  CompileErrorContext *m_error_context;
  LogicalSourceLocationPtr m_error_location;

  PhysicalSourceLocation m_location;
  const char *m_current, *m_end;
  const char *m_token_start;
};

template<typename Id, typename Value>
class LexerValue {
  Id m_id;
  PhysicalSourceLocation m_location;
  Value m_value;
  
public:
  typedef Id IdType;
  typedef Value ValueType;
  
  LexerValue() {}
  
  LexerValue(int id, PhysicalSourceLocation location)
  : m_id(id), m_location(location) {
  }
  
  template<typename U>
  LexerValue(int id, PhysicalSourceLocation location, const U& value)
  : m_id(id), m_location(location), m_value(value) {
  }
  
  /// \brief Get the ID of this token
  const IdType& id() const {return m_id;}
  /// \brief Get the physical location of this token
  const PhysicalSourceLocation& location() const {return m_location;}
  /// \brief Get the value of this token
  const ValueType& value() const {return m_value;}
  /// \brief Get the value of this token
  ValueType& value() {return m_value;}
};

template<std::size_t backtrack, typename Id, typename ValueArg, typename Callback>
class Lexer {
public:
  static const std::size_t n_backtrack = backtrack;
  typedef Id IdType;
  typedef LexerValue<Id, ValueArg> ValueType;
  typedef Callback CallbackType;

  Lexer(CompileErrorContext& error_context, const SourceLocation& loc, const char* start, const char* end, const Callback& callback=Callback())
  : m_position(error_context, loc, start, end), m_callback(callback) {
    // Grab first token
    m_values[0] = m_callback.lex(m_position);
    m_values_begin = 0;
    m_values_pos = 0;
    m_values_end = 1;
  }
  
  PSI_ATTRIBUTE((PSI_NORETURN)) void error(const PhysicalSourceLocation& loc, const ErrorMessage& message) {m_position.error(loc, message);}
  CompileErrorPair error_loc(const PhysicalSourceLocation& loc) {return m_position.error_loc(loc);}

  /**
   * \brief Lexer value \c n items back
   * 
   * This does not currently do proper error checking to see whether \c n is out
   * of bounds as defined by \c m_values_begin and \c m_values_end.
   */
  ValueType& value(unsigned n=0) {
    PSI_ASSERT(n < n_backtrack);
    ++n;
    
    unsigned idx = m_values_pos;
    if (idx >= n)
      idx -= n;
    else
      idx += n_backtrack + 1 - n;
    return m_values[idx];
  }

  /// \brief Peek at the next token
  ValueType& peek() {
    return m_values[m_values_pos];
  }

  /// \brief Accept the next token unconditionally
  void accept() {
    m_values_pos = next_values_pos(m_values_pos);
    
    if (m_values_pos == m_values_end) {
      m_values[m_values_pos] = m_callback.lex(m_position);
      
      if (m_values_pos == m_values_begin)
        m_values_begin = next_values_pos(m_values_begin);

      m_values_end = next_values_pos(m_values_end);
    }
  }

  /**
    * \brief Put the previous token back into the token queue.
    *
    * Note that this asserts that there is an element to be pushed back.
    */
  void back() {
    PSI_ASSERT(m_values_pos != m_values_begin);
    if (m_values_pos == 0)
      m_values_pos = n_backtrack;
    else
      --m_values_pos;
  }

  /// \brief Return true if the next token is not \c t
  bool reject(const IdType& t) {
    return peek().id() != t;
  }

  /// \brief Accept the next token if it is a \c t
  bool accept(const IdType& t) {
    if (peek().id() == t) {
      accept();
      return true;
    }
    
    return false;
  }

  /// \brief Accept or reject two tokens as a pair.
  bool accept2(const IdType& a, const IdType& b) {
    if (accept(a)) {
      if (accept(b))
        return true;
      else
        back();
    }
    
    return false;
  }
  
  /// \brief Require the next token to be a \c t
  void expect(const IdType& t) {
    if (peek().id() != t)
      error(peek().location(), boost::format("Unexpected token %s, expected %s") % m_callback.error_name(peek()) % m_callback.error_name(t));
    accept();
  }

  PSI_ATTRIBUTE((PSI_NORETURN)) void unexpected() {
    error(peek().location(), boost::format("Unexpected token %s") % m_callback.error_name(peek()));
  }

  /// \brief Get the location of the next token.
  const PhysicalSourceLocation& loc_begin() {
    return peek().location();
  }
  
  /// \brief Update the given location, which should have been returned by \c loc_begin, to include up to the end of the last token.
  void loc_end(PhysicalSourceLocation& loc) {
    loc.last_line = value().location().last_line;
    loc.last_column = value().location().last_column;
  }

private:
  LexerPosition m_position;
  Callback m_callback;
  ValueType m_values[n_backtrack + 1];
  std::size_t m_values_pos, m_values_begin, m_values_end;
  
  static std::size_t next_values_pos(std::size_t idx) {
    return idx < n_backtrack ? idx + 1 : 0;
  }
};
}

#endif
