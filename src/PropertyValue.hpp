#ifndef HPP_PSI_PROPERTYVALUE
#define HPP_PSI_PROPERTYVALUE

#include <map>
#include <string>
#include <vector>
#include <boost/optional.hpp>

#include "Export.hpp"
#include "Runtime.hpp"
#include "Utility.hpp"

namespace Psi {
class PropertyValue;

/**
 * Property map object. This is equivalent to a JSON object.
 */
typedef PSI_STD::map<String, PropertyValue> PropertyMap;
typedef PSI_STD::vector<PropertyValue> PropertyList;

struct PropertyValueNull {};

namespace {
  const PropertyValueNull property_null = {};
};

/**
 * JSON-esque value.
 */
class PSI_COMPILER_COMMON_EXPORT PropertyValue {
public:
  enum Type {
    t_null,
    t_boolean,
    t_integer,
    t_real,
    t_str,
    t_map,
    t_list
  };
  
private:
  Type m_type;
  
  union {
    int integer;
    double real;
    AlignedStorageFor<PropertyMap> map;
    AlignedStorageFor<PropertyList> list;
    AlignedStorageFor<String> str;
  } m_value;
  
  void assign(const PropertyValue& src);
  void assign(const std::string& src);
  void assign(const String& src);
  void assign(const char *src);
  void assign(bool src);
  void assign(int src);
  void assign(double src);
  void assign(const PropertyMap& src);
  void assign(const PropertyList& src);

  const PropertyValue *path_value(const std::string& name) const;
  
public:
  PropertyValue() : m_type(t_null) {}
  PropertyValue(const PropertyValueNull&) : m_type(t_null) {}
  PropertyValue(const PropertyValue& src) : m_type(t_null) {assign(src);}
  PropertyValue(const std::string& src) : m_type(t_null) {assign(src);}
  PropertyValue(const String& src) : m_type(t_null) {assign(src);}
  PropertyValue(const char *src) : m_type(t_null) {assign(src);}
  PropertyValue(bool src) : m_type(t_null) {assign(src);}
  PropertyValue(int src) : m_type(t_null) {assign(src);}
  PropertyValue(double src) : m_type(t_null) {assign(src);}
  PropertyValue(const PropertyMap& src) : m_type(t_null) {assign(src);}
  PropertyValue(const PropertyList& src) : m_type(t_null) {assign(src);}
  ~PropertyValue();
  
  /// \brief Whether this value is null.
  bool null() const {return m_type == t_null;}
  /// \brief This value's type.
  Type type() const {return m_type;}
  /// \brief Set this value to NULL.
  void reset();
  
  PropertyValue& operator = (const PropertyValueNull&) {reset(); return *this;}
  PropertyValue& operator = (const PropertyValue& src) {assign(src); return *this;}
  PropertyValue& operator = (const std::string& src) {assign(src); return *this;}
  PropertyValue& operator = (const String& src) {assign(src); return *this;}
  PropertyValue& operator = (const char *src) {assign(src); return *this;}
  PropertyValue& operator = (bool src) {assign(src); return *this;}
  PropertyValue& operator = (int src) {assign(src); return *this;}
  PropertyValue& operator = (double src) {assign(src); return *this;}
  PropertyValue& operator = (const PropertyMap& src) {assign(src); return *this;}
  PropertyValue& operator = (const PropertyList& src) {assign(src); return *this;}
  
  bool boolean() const {PSI_ASSERT(m_type == t_boolean); return (m_value.integer != 0);}
  int integer() const {PSI_ASSERT(m_type == t_integer); return m_value.integer;}
  double real() const {PSI_ASSERT(m_type == t_real); return m_value.real;}
  const String& str() const {PSI_ASSERT(m_type == t_str); return *m_value.str.ptr();}
  
  PropertyMap& map() {PSI_ASSERT(m_type == t_map); return *m_value.map.ptr();}
  const PropertyMap& map() const {PSI_ASSERT(m_type == t_map); return *m_value.map.ptr();}
  const PropertyValue& get(const String& key) const;
  bool has_key(const String& key) const;
  PropertyValue& operator [] (const String& key);

  PropertyList& list() {PSI_ASSERT(m_type == t_list); return *m_value.list.ptr();}
  const PropertyList& list() const {PSI_ASSERT(m_type == t_list); return *m_value.list.ptr();}
  std::vector<std::string> str_list() const;
  
  boost::optional<std::string> path_str(const std::string& key) const;

  static PropertyValue parse(const char *begin, const char *end);
  static PropertyValue parse(const char *s);
  void parse_configuration(const char *begin, const char *end);
};

PSI_COMPILER_COMMON_EXPORT bool operator == (const PropertyValue& lhs, const PropertyValue& rhs);
PSI_COMPILER_COMMON_EXPORT bool operator == (const PropertyValue& lhs, const String& rhs);
PSI_COMPILER_COMMON_EXPORT bool operator == (const String& lhs, const PropertyValue& rhs);
PSI_COMPILER_COMMON_EXPORT bool operator == (const PropertyValue& lhs, const char *rhs);
PSI_COMPILER_COMMON_EXPORT bool operator == (const char *lhs, const PropertyValue& rhs);

PSI_VISIT_SIMPLE(PropertyValue)
}

#endif
