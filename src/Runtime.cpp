#include "Runtime.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <iostream>

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
    os << str.c_str();
    return os;
  }
}
