#include "Runtime.hpp"

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

  String::String() {
    m_c.length = 0;
    m_c.data = "\0";
  }

  String::String(const String& src) {
    m_c.length = src.length;
    if (m_c.length) {
      m_c.data = checked_alloc(m_c.length + 1);
      std::memcpy(m_c.data, src.m_c.data, m_c.length+1);
    } else {
      m_c.data = "\0";
    }
  }

  String::String(MoveRef<String> src) {
    m_c = src->m_c;
    src->m_c.length = 0;
    src->m_c.data = "\0";
  }

  String::String(const char *s) {
    m_c.length = std::strlen(s);
    if (m_c.length) {
      m_c.data = checked_alloc(m_c.length+1);
      std::memcpy(m_c.data, s, m_c.length+1);
    } else {
      m_c.data = "\0";
    }
  }

  String::String(const char *s, std::size_t n) {
    m_c.length = n;
    if (m_c.length) {
      m_c.data = checked_alloc(m_c.length+1);
      std::memcpy(m_c.data, s, m_c.length);
      m_c.data[m_c.length] = '\0';
    } else {
      m_c.data = "\0";
    }
  }

  String::~String() {
    clear();
  }
  
  String& String::operator = (const String& src) {
    String copy(src);
    operator = (MoveRef<String>(copy));
    return *this;
  }

  String& String::operator = (MoveRef<String> src) {
    clear();
    m_c = src->m_c;
    src->m_c.length = 0;
    src->m_c.data = "\0";
    return *this;
  }
  
  String& String::operator = (const char *s) {
    String copy(s);
    operator = (MoveRef<String>(copy));
    return *this;
  }
  
  void String::clear() {
    if (m_c.length) {
      checked_free(m_c.data);
      m_c.length = 0;
      m_c.data = "\0";
    }
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
    std::size_t n = std::min(m_c.length, rhs.m_c.length);
    int o = std::memcmp(m_c.data, rhs.m_c.data, std::min(m_c.length, rhs.m_c.length));
    if (o < 0)
      return true;
    else if ((o == 0) && (m_c.length < rhs.m_c.length))
      return true;
    else
      return false;
  }
}
