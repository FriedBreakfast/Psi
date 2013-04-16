#include "Runtime.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Psi {
  void* checked_alloc(std::size_t n) {
    void *ptr = std::malloc(n);
    if (!ptr)
      throw std::bad_alloc();
    return ptr;
  }
  
  void checked_free(std::size_t, void *ptr) {
    std::free(ptr);
  }
  
  namespace {
    char null_str[] = "";
  }

  String::String() {
    m_c.length = 0;
    m_c.data = null_str;
  }

  String::String(const String& src) {
    m_c.length = src.m_c.length;
    if (m_c.length) {
      m_c.data = static_cast<char*>(checked_alloc(m_c.length + 1));
      std::memcpy(m_c.data, src.m_c.data, m_c.length+1);
    } else {
      m_c.data = null_str;
    }
  }
  
  void String::init(const char *s, std::size_t n) {
    m_c.length = n;
    if (m_c.length) {
      m_c.data = static_cast<char*>(checked_alloc(m_c.length+1));
      std::memcpy(m_c.data, s, m_c.length);
      m_c.data[m_c.length] = '\0';
    } else {
      m_c.data = null_str;
    }
  }

  String::String(const char *s) {
    init(s, std::strlen(s));
  }

  String::String(const char *begin, const char *end) {
    init(begin, end - begin);
  }

  String::String(const char *s, std::size_t n) {
    init(s, n);
  }

  String::String(const std::string& s) {
    init(s.c_str(), s.size());
  }

  String::~String() {
    clear();
  }
  
  String& String::operator = (const String& src) {
    String(src).swap(*this);
    return *this;
  }

  String& String::operator = (const char *s) {
    String(s).swap(*this);
    return *this;
  }
  
  void String::clear() {
    if (m_c.length) {
      checked_free(m_c.length + 1, m_c.data);
      m_c.length = 0;
      m_c.data = null_str;
    }
  }

  void String::swap(String& other) {
    std::swap(m_c, other.m_c);
  }

  bool String::operator == (const String& rhs) const {
    if (m_c.length != rhs.m_c.length)
      return false;
    return std::memcmp(m_c.data, rhs.m_c.data, m_c.length) == 0;
  }
  
  bool String::operator != (const String& rhs) const {
    return !operator == (rhs);
  }

  bool String::operator < (const String& rhs) const {
    int o = std::memcmp(m_c.data, rhs.m_c.data, std::min(m_c.length, rhs.m_c.length));
    if (o < 0)
      return true;
    else if ((o == 0) && (m_c.length < rhs.m_c.length))
      return true;
    else
      return false;
  }

  String::operator std::string() const {
    return std::string(c_str());
  }

  bool operator == (const String& lhs, const char *rhs) {
    return strncmp(lhs.c_str(), rhs, lhs.length()) == 0;
  }
  
  bool operator == (const char *lhs, const String& rhs) {
    return strncmp(lhs, rhs.c_str(), rhs.length()) == 0;
  }

  bool operator == (const String& lhs, const std::string& rhs) {
    return lhs.c_str() == rhs;
  }

  bool operator == (const std::string& lhs, const String& rhs) {
    return lhs == rhs.c_str();
  }
  
  std::ostream& operator << (std::ostream& os, const String& str) {
    if (!str.empty())
      os << str.c_str();
    return os;
  }
  
  namespace {
    /**
     * Grab up to n characters from the iterator pair and insert null terminator (in addition to the characters grabbed).
     * 
     * Return number of characters grabbed
     */
    unsigned grab_digits_up_to(char *out, unsigned n, std::vector<char>::const_iterator& cur, const std::vector<char>::const_iterator& end) {
      unsigned c = 0;
      for (; c != n; ++c) {
        if (cur == end)
          break;
        if (!c_isdigit(*cur))
          break;
        
        out[c] = *cur;
        ++cur;
      }
      
      out[c] = '\0';
      return c;
    }
  }
  
  /**
   * Encode a Unicode character to UTF-8.
   */
  void unicode_encode(std::vector<char>& output, unsigned value) {
    unsigned n_continuation;
    unsigned char high_bits;
    if (value < 0x80) {
      output.push_back(value);
      return;
    } else if (value < 0x800) {
      n_continuation = 1;
      high_bits = 0xC0;
    } else if (value < 0x10000) {
      n_continuation = 2;
      high_bits = 0xE0;
    } else if (value < 0x200000) {
      n_continuation = 3;
      high_bits = 0xF0;
    } else if (value < 0x4000000) {
      n_continuation = 4;
      high_bits = 0xF8;
    } else {
      PSI_ASSERT(value < 0x80000000);
      n_continuation = 5;
      high_bits = 0xFC;
    }
    
    char buf[6];
    unsigned shifted_value = value;
    for (unsigned i = 0; i < n_continuation; ++i) {
      buf[i] = 0x80 | (shifted_value & 0x3F);
      shifted_value >>= 6;
    }
    buf[n_continuation] = high_bits | shifted_value;
    
    std::reverse(buf, buf + n_continuation + 1);
    output.insert(output.end(), buf, buf + n_continuation + 1);
  }
  
  /**
   * \brief Process escape codes in a string.
   */
  std::vector<char> string_unescape(const std::vector<char>& s) {
    const char *escape_list = "\\nrtv";
    const char *escape_replace = "\\\n\r\t\v";
    
    std::vector<char> r;
    for (std::vector<char>::const_iterator ii = s.begin(), ie = s.end(); ii != ie;) {
      if (*ii == '\\') {
        ++ii;
        if (ii == ie) {
          r.push_back('\\');
          break;
        }
        
        // Handle escape codes
        if (const char *p = std::strchr(escape_list, *ii)) {
          r.push_back(escape_replace[p-escape_list]);
          ++ii;
        } else if (*ii == 'u') {
          // Unicode escapes
          ++ii;
          char data[5];
          grab_digits_up_to(data, 5, ii, ie);
          unicode_encode(r, strtol(data, NULL, 16));
        } else if (*ii == 'x') {
          // ASCII escapes
          ++ii;
          char data[3];
          grab_digits_up_to(data, 3, ii, ie);
          unicode_encode(r, strtol(data, NULL, 16));
        }
      } else {
        unsigned char uc = *ii;
        if (uc > 127) // Treat  >= 128 as ISO8859-1
          unicode_encode(r, uc);
        else
          r.push_back(*ii);
        ++ii;
      }
    }
    
    return r;
  }
}
