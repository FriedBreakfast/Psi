#include "PropertyValue.hpp"

#include <locale>
#include <sstream>
#include <stdio.h>
#include <boost/format.hpp>

namespace Psi {
/**
 * \brief Test whether two floating point values are equivalent.
 * 
 * This means that NaN==NaN, however distinguishing between quiet
 * and signalling NaN is not supported.
 * 
 * This routine checks that both are the same type according to fpclassify(),
 * have the same sign according to signbit() and if finite, have the same value.
 */
bool fpequiv(double a, double b) {
#if defined(_MSC_VER)
  if (_finite(a))
    return a == b;
  else
    return (_fpclass(a) == _fpclass(b));
#else
  if (std::isfinite(a))
    return a == b;
  else // Handling Inf and NaN is more complicated
    return (std::fpclassify(a) == std::fpclassify(b)) && (std::signbit(a) == std::signbit(b));
#endif
}

PropertyValue::~PropertyValue() {
  reset();
}

void PropertyValue::reset() {
  switch (m_type) {
  case t_null:
  case t_boolean:
  case t_integer:
  case t_real: break;
  case t_str: m_value.str.ptr()->~String(); break;
  case t_map: m_value.map.ptr()->~PropertyMap(); break;
  case t_list: m_value.list.ptr()->~PropertyList(); break;
  default: PSI_FAIL("unknown value type");
  }
  
  m_type = t_null;
}

void PropertyValue::assign(const PropertyValue& src) {
  switch (src.m_type) {
  case t_null: reset(); break;
  case t_boolean: assign(src.boolean()); break;
  case t_integer: assign(src.integer()); break;
  case t_real: assign(src.real()); break;
  case t_str: assign(src.str()); break;
  case t_map: assign(src.map()); break;
  case t_list: assign(src.list()); break;
  default: PSI_FAIL("unknown value type");
  }
}

void PropertyValue::assign(const std::string& src) {
  assign(String(src));
}

void PropertyValue::assign(const String& src) {
  if (m_type != t_str) {
    reset();
    new (m_value.str.ptr()) String(src);
    m_type = t_str;
  } else {
    *m_value.str.ptr() = src;
  }
}

void PropertyValue::assign(const char *src) {
  assign(String(src));
}

void PropertyValue::assign(bool src) {
  if (m_type != t_boolean) {
    reset();
    m_type = t_boolean;
  }
  
  m_value.integer = src ? 1:0;
}

void PropertyValue::assign(int src) {
  if (m_type != t_integer) {
    reset();
    m_type = t_integer;
  }
  
  m_value.integer = src;
}

void PropertyValue::assign(double src) {
  if (m_type != t_real) {
    reset();
    m_type = t_real;
  }
  
  m_value.real = src;
}

void PropertyValue::assign(const PropertyMap& src) {
  if (m_type != t_map) {
    reset();
    new (m_value.map.ptr()) PropertyMap(src);
    m_type = t_map;
  } else {
    *m_value.map.ptr() = src;
  }
}

void PropertyValue::assign(const PropertyList& src) {
  if (m_type != t_list) {
    reset();
    new (m_value.list.ptr()) PropertyList(src);
    m_type = t_list;
  } else {
    *m_value.list.ptr() = src;
  }
}

const PropertyValue& PropertyValue::get(const String& key) const {
  if (type() != t_map)
    throw std::runtime_error("Property value is not a map");
  
  PropertyMap::const_iterator it = map().find(key);
  if (it == map().end())
    throw std::runtime_error("Property map does not contain key: " + std::string(key));
  
  return it->second;
}

bool PropertyValue::has_key(const String& key) const {
  if (type() != t_map)
    return false;
  
  PropertyMap::const_iterator it = map().find(key);
  return (it != map().end());
}

PropertyValue& PropertyValue::operator [] (const String& key) {
  if (type() != t_map)
    assign(PropertyMap());

  return (*m_value.map.ptr())[key];
}

std::vector<std::string> PropertyValue::str_list() const {
  if (type() != t_list)
    throw std::runtime_error("Property value is not a list");
  
  std::vector<std::string> result;
  for (PropertyList::const_iterator ii = list().begin(), ie = list().end(); ii != ie; ++ii) {
    if (ii->type() != t_str)
      throw std::runtime_error("Property value list element is not a string");
    result.push_back(ii->str());
  }
  
  return result;
}

const PropertyValue *PropertyValue::path_value(const std::string& key) const {
  if (key.empty())
    return this;

  const PropertyValue *pv = this;
  for (std::size_t pos = 0; ; ) {
    if (pv->type() != t_map)
      return NULL;
    std::size_t next_pos = key.find('.', pos);
    std::string part = key.substr(pos, next_pos);
    PropertyMap::const_iterator ci = pv->m_value.map.ptr()->find(part);
    if (ci == pv->m_value.map.ptr()->end())
      return NULL;
    pv = &ci->second;
    if (next_pos == std::string::npos)
      return pv;
    pos = next_pos + 1;
  }
}

boost::optional<std::string> PropertyValue::path_str(const std::string& key) const {
  const PropertyValue *pv = path_value(key);
  if (pv && (pv->type() == t_str))
    return std::string(pv->str());
  return boost::none;
}

bool PropertyValue::path_bool(const std::string& key) const {
  const PropertyValue *pv = path_value(key);
  if (!pv)
    return false;
  return (pv->type() == t_boolean) && pv->boolean();
}

bool operator == (const PropertyValue& lhs, const PropertyValue& rhs) {
  if (lhs.type() != rhs.type())
    return false;
  
  switch (lhs.type()) {
  case PropertyValue::t_null: return true;
  case PropertyValue::t_boolean: return lhs.boolean() == rhs.boolean();
  case PropertyValue::t_integer: return lhs.integer() == rhs.integer();
  case PropertyValue::t_real: return fpequiv(lhs.real(), rhs.real());
  case PropertyValue::t_str: return lhs.str() == rhs.str();
  case PropertyValue::t_map: return lhs.map() == rhs.map();
  case PropertyValue::t_list: return lhs.list() == rhs.list();
  default: PSI_FAIL("unknown value type");
  }
}

bool operator == (const PropertyValue& lhs, const String& rhs) {
  return (lhs.type() == PropertyValue::t_str) && (lhs.str() == rhs);
}

bool operator == (const String& lhs, const PropertyValue& rhs) {
  return (rhs.type() == PropertyValue::t_str) && (rhs.str() == lhs);
}

bool operator == (const PropertyValue& lhs, const char *rhs) {
  return (lhs.type() == PropertyValue::t_str) && (lhs.str() == rhs);
}

bool operator == (const char *lhs, const PropertyValue& rhs) {
  return (rhs.type() == PropertyValue::t_str) && (rhs.str() == lhs);
}

PropertyValueParseError::PropertyValueParseError(unsigned line, unsigned column, const std::string& message)
: m_line(line), m_column(column), m_message(message) {
}

PropertyValueParseError::~PropertyValueParseError() throw() {
}

const char* PropertyValueParseError::what() const throw() {
  return m_message.c_str();
}

namespace {
class ParseHelper {
  const char *m_current, *m_end;
  bool m_skip_whitespace;
  bool m_allow_comments;
  unsigned m_line, m_column;
  char m_next;

  void to_next() {
    if (m_skip_whitespace)
      skip_whitespace();
  }

  void next_char() {
    PSI_ASSERT(m_current != m_end);
    ++m_current;
    ++m_column;
    m_next = (m_current == m_end) ? '\0' : *m_current;
  }

public:
  ParseHelper(const char *begin, const char *end, bool allow_comments, unsigned first_line, unsigned first_column)
  : m_current(begin), m_end(end), m_skip_whitespace(true), m_allow_comments(allow_comments),
  m_line(first_line), m_column(first_column) {
    m_next = (m_current != m_end) ? *m_current : '\0';
    to_next();
  }

  /// Peek at the next character
  char peek() {
    return m_next;
  }

  bool end() const {
    return m_current == m_end;
  }
  
  unsigned line() const {return m_line;}
  unsigned column() const {return m_column;}

  /// Skip any whitespace characters at the current point
  void skip_whitespace() {
    const std::locale& c_locale = std::locale::classic();
    bool in_comment = false;
    while(!end()) {
      char c = peek();
      if (c == '\n') {
        m_line++;
        m_column = 0;
        in_comment = false;
        next_char();
      } else if (m_allow_comments && (c == '#')) {
        in_comment = true;
        next_char();
      } else if (in_comment || std::isspace(c, c_locale)) {
        next_char();
      } else {
        break;
      }
    }
  }

  /// Enable or disable automatic whitespace skipping
  void set_skip_whitespace(bool s) {
    m_skip_whitespace = s;
    if (m_skip_whitespace)
      skip_whitespace();
  }

  /// Test if the next token is a certain character, and accept it if it is
  bool accept(char c) {
    if (peek() == c) {
      accept();
      return true;
    } else {
      return false;
    }
  }

  /// Accept the next character (unconditionally)
  void accept() {
    next_char();
    to_next();
  }

  /// Require the next character is a particular one, else throw an exception
  void expect(char c) {
    if (!accept(c))
      throw_error(boost::format("Expected '%c'") % c);
  }
  
  template<typename T>
  PSI_ATTRIBUTE((PSI_NORETURN)) void throw_error(const T& message) {
    std::stringstream ss;
    ss << message;
    throw PropertyValueParseError(m_line, m_column, ss.str());
  }
};

PropertyValue json_parse_element(ParseHelper& tokener);

std::string json_parse_string(ParseHelper& tokener) {
  std::vector<char> s;
  tokener.set_skip_whitespace(false);
  tokener.expect('\"');
  while (true) {
    if (tokener.end()) {
      tokener.throw_error("Unexpected end of JSON data");
    } else if (tokener.accept('\\')) {
      if (tokener.end()) {
        tokener.throw_error("Unexpected end of JSON data after '\\'");
      } else if (tokener.peek() == 'u') {
        char digits[5];
        for (unsigned i = 0; i != 4; ++i) {
          if (tokener.end())
            tokener.throw_error("Unexpected end of data in '\\u': expected 4 digits");
          char c = tokener.peek();
          digits[i] = tokener.peek();
          if ((c >= '0') && (c <= '9')) {
            digits[i] = c;
            tokener.accept();
          } else {
            tokener.throw_error(boost::format("Expected 4 digits after '\\u' but got a '%c'") % tokener.peek());
          }
        }
        digits[5] = '\0'; 
        unicode_encode(s, atoi(digits));
      } else {
        char esc;
        switch (tokener.peek()) {
        default: tokener.throw_error(boost::format("Unknown escape character '%c'") % tokener.peek());
        case '\"': esc = '\"'; break;
        case '\\': esc = '\\'; break;
        case '/': esc = '/'; break;
        case 'b': esc = '\b'; break;
        case 'f': esc = '\f'; break;
        case 'n': esc = '\n'; break;
        case 'r': esc = '\r'; break;
        case 't': esc = '\t'; break;
        case '0': esc = '\0'; break;
        }
        s.push_back(esc);
        tokener.accept();
      }
    } else if (tokener.accept('\"')) {
      break;
    } else {
      s.push_back(tokener.peek());
      tokener.accept();
    }
  }
  tokener.set_skip_whitespace(true);
  return std::string(s.begin(), s.end());
}

std::string json_parse_keyword(ParseHelper& tokener) {
  std::string s;
  const std::locale& c_locale = std::locale::classic();
  tokener.set_skip_whitespace(false);
  while (std::isalnum(tokener.peek(), c_locale) || std::strchr("!$%^&*@~?<>/_", tokener.peek())) {
    s.push_back(tokener.peek());
    tokener.accept();
  }
  tokener.set_skip_whitespace(true);
  return s;
}

/**
 * Parse an object member key. This may be a string or an identifier, which
 * will be treated as a string (this contravenes the JSON spec which requires
 * a string).
 */
String json_parse_key(ParseHelper& helper) {
  if (helper.peek() == '\"')
    return json_parse_string(helper);
  else
    return json_parse_keyword(helper);
}

PropertyValue json_parse_number(ParseHelper& tokener) {
  const std::locale& c_locale = std::locale::classic();
  unsigned line = tokener.line(), column = tokener.column();
  bool real = false;
  std::string digits;
  tokener.set_skip_whitespace(false);
  while (true) {
    char c = tokener.peek();
    if (std::isspace(c, c_locale))
      break;
    if ((c == '.') || (c == 'e') || (c == 'E'))
      real = true;
    digits.push_back(c);
    tokener.accept();
  }
  tokener.set_skip_whitespace(true);

  if (real) {
    unsigned count = 0;
    double value;
    sscanf(digits.c_str(), "%lf%n", &value, &count);
    if (count != digits.length())
      throw PropertyValueParseError(line, column, "Error parsing number");
    return value;
  } else {
    unsigned count = 0;
    int value;
    sscanf(digits.c_str(), "%d%n", &value, &count);
    if (count != digits.length())
      throw PropertyValueParseError(line, column, "Error parsing number");
    return value;
  }
}

PropertyMap json_parse_object(ParseHelper& tokener, bool as_root=false) {
  PropertyMap entries;
  while (true) {
    if (as_root ? tokener.end() : (tokener.peek() == '}'))
      return entries;
    String key = json_parse_key(tokener);
    tokener.expect(':');
    PropertyValue value = json_parse_element(tokener);
    entries.insert(std::make_pair(key, value));
    tokener.accept(',');
  }
}

PropertyList json_parse_array(ParseHelper& tokener, bool as_root=false) {
  PropertyList entries;
  while (true) {
    if (as_root ? tokener.end() : (tokener.peek() == ']'))
      return entries;
    entries.push_back(json_parse_element(tokener));
    tokener.accept(',');
  }
}

PropertyValue json_parse_element(ParseHelper& tokener) {
  const std::locale& c_locale = std::locale::classic();
  if (tokener.accept('{')) {
    PropertyValue result = json_parse_object(tokener);
    tokener.expect('}');
    return result;
  } else if (tokener.accept('[')) {
    PropertyValue result = json_parse_array(tokener);
    tokener.expect(']');
    return result;
  } else if (tokener.peek() == '\"') {
    return json_parse_string(tokener);
  } else if ((tokener.peek() == '-') || std::isdigit(tokener.peek(), c_locale)) {
    return json_parse_number(tokener);
  } else {
    std::string s = json_parse_keyword(tokener);
    if (s == "null") return PropertyValue();
    else if (s == "true") return true;
    else if (s == "false") return false;
    else tokener.throw_error(boost::format("Unknown JSON element '%s'") % s);
  }
}
}

PropertyValue PropertyValue::parse(const char *begin, const char *end, unsigned first_line, unsigned first_column) {
  ParseHelper tokener(begin, end, false, first_line, first_column);
  PropertyMap pv;
  pv = json_parse_object(tokener, true);
  if (!tokener.end())
    tokener.throw_error("Extra tokens at end of JSON data");
  return pv;
}

PropertyValue PropertyValue::parse(const char *s) {
  return parse(s, s+std::strlen(s));
}

/**
 * \brief Parse a configuration file and update an existing PropertyValue map with the results.
 */
void PropertyValue::parse_configuration(const char *begin, const char *end, unsigned first_line, unsigned first_column) {
  ParseHelper tokener(begin, end, true, first_line, first_column);
  while (!tokener.end()) {
    PropertyValue *location = this;
    while (true) {
      String name = json_parse_key(tokener);
      if (location->type() != PropertyValue::t_map)
        *location = PropertyMap();
      location = &location->map()[name];

      if (tokener.accept('.')) {
        continue;
      } else if (tokener.accept('=')) {
        *location = json_parse_element(tokener);
        break;
      } else {
        tokener.throw_error(boost::format("Unexpected character '%c'") % tokener.peek());
      }
    }
  }
}
}
