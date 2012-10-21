#include "Runtime.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <json/json_tokener.h>
#include <json/linkhash.h>
#include <json/arraylist.h>

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
    if (std::isfinite(a))
      return a == b;
    else // Handling Inf and NaN is more complicated
      return (std::fpclassify(a) == std::fpclassify(b)) && (std::signbit(a) == std::signbit(b));
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
    os << str.c_str();
    return os;
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
  
  namespace {
    PropertyValue to_property_value(json_object *obj) {
      switch (json_object_get_type(obj)) {
      case json_type_null: return property_null;
      case json_type_boolean: return bool(json_object_get_boolean(obj));
      case json_type_int: return json_object_get_int(obj);
      case json_type_double: return json_object_get_double(obj);
      case json_type_string: return String(json_object_get_string(obj));
        
      case json_type_array: {
        array_list *ar = json_object_get_array(obj);
        std::vector<PropertyValue> elements(ar->length);
        for (unsigned ii = 0, ie = ar->length; ii != ie; ++ii)
          elements[ii] = to_property_value(static_cast<json_object*>(ar->array[ii]));
        return elements;
      }
        
      case json_type_object: {
        std::map<String, PropertyValue> elements;
        lh_table *tbl = json_object_get_object(obj);
        for (lh_entry *ii = tbl->head; ii; ii = ii->next)
          elements.insert(std::make_pair(String(static_cast<char*>(ii->k)),
                                         to_property_value(static_cast<json_object*>(const_cast<void*>(ii->v)))));
        return elements;
      }

      default:
        PSI_FAIL("Unrecognised json object type");
      }
    }
  }
  
  PropertyValue PropertyValue::parse(const char *begin, const char *end) {
    std::size_t buffer_size = 4096;
    std::vector<char> buffer(buffer_size);
    
    boost::shared_ptr<json_tokener> tok(json_tokener_new(), json_tokener_free);
    boost::shared_ptr<json_object> obj_ptr;
    
    for (const char *ptr = begin; ptr != end;) {
      std::size_t count = std::min(std::ptrdiff_t(buffer_size), end-ptr);
      buffer.assign(ptr, ptr+count);
      
      json_object *obj = json_tokener_parse_ex(tok.get(), buffer.data(), count);
      ptr += tok->char_offset;
      
      if (obj) {
        if (obj_ptr)
          throw std::runtime_error("Multiple JSON objects in property value data");
        obj_ptr.reset(obj, json_object_put);
      } else if (tok->err != json_tokener_continue) {
        throw std::runtime_error("JSON parse error");
      }
    }
    
    if (!obj_ptr)
      throw std::runtime_error("JSON parse error");
    
    return to_property_value(obj_ptr.get());
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
