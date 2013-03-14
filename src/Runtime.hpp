#ifndef HPP_PSI_RUNTIME
#define HPP_PSI_RUNTIME

#include <utility>
#include <algorithm>
#include <iosfwd>
#include <boost/aligned_storage.hpp>
#include <boost/static_assert.hpp>

#include <vector>
#include <map>

#include "Assert.hpp"
#include "CppCompiler.hpp"
#include "Export.hpp"
#include "Utility.hpp"
#include "Visitor.hpp"

/**
 * \file
 * 
 * This file contains the C++ interface to various Psi types. Most of these
 * mirror existing C++ types but I need a portable binary interface so using
 * the C++ types is out of the question.
 */

namespace Psi {
  PSI_COMPILER_COMMON_EXPORT void* checked_alloc(std::size_t n);
  PSI_COMPILER_COMMON_EXPORT void checked_free(std::size_t n, void *ptr);

  template<std::size_t N>
  class StackBuffer : boost::noncopyable {
    PSI_ATTRIBUTE((PSI_ALIGNED_MAX)) char m_buffer[N];
    void *m_ptr;
    std::size_t m_size;

  public:
    StackBuffer(std::size_t m) : m_size(m) {
      if (m <= N)
        m_ptr = &m_buffer;
      else
        m_ptr = checked_alloc(m);
    }

    ~StackBuffer() {
      if (m_size > N)
        checked_free(m_size, m_ptr);
    }

    void* get() {return m_ptr;}
  };

  typedef char PsiBool;
  typedef std::size_t PsiSize;

  enum PsiBoolValues {
    psi_false = 0,
    psi_true = 1
  };

  struct SharedPtrOwner;
  
  struct SharedPtrOwnerVTable {
    void (*destroy) (SharedPtrOwner*);
  };
  
  struct SharedPtrOwner {
    SharedPtrOwnerVTable *vptr;
    PsiSize use_count;
  };
  
  /**
   * \brief C layout of SharedPtr.
   */
  struct SharedPtr_C {
    void *ptr;
    SharedPtrOwner *owner;
  };

  template<typename U>
  struct SharedPtrOwnerImpl : SharedPtrOwner {
    SharedPtrOwnerVTable vtable;
    U *ptr;
  };

  template<typename U>
  void shared_ptr_delete(SharedPtrOwner *owner) {
    SharedPtrOwnerImpl<U> *self = static_cast<SharedPtrOwnerImpl<U>*>(owner);
    delete self->ptr;
    delete self;
  }

  /**
   * \brief Shared pointer class.
   *
   * This wraper SharedPtr_C, and is designed to be interface-compatible
   * with boost::shared_ptr, excluding custom allocator stuff.
   */
  template<typename T>
  class SharedPtr {
    SharedPtr_C m_c;

    void init(T *ptr, SharedPtrOwner *owner) {
      m_c.ptr = ptr;
      m_c.owner = owner;
      if (owner)
        ++owner->use_count;
    }

    typedef void (SharedPtr::*safe_bool_type) () const;
    void safe_bool_true() const {}

  public:
    template<typename> friend class SharedPtr;
    typedef T value_type;

    SharedPtr() {
      m_c.ptr = 0;
      m_c.owner = 0;
    }

    template<typename U>
    SharedPtr(U *ptr) {
      SharedPtrOwnerImpl<U> *owner;
      try {
        owner = new SharedPtrOwnerImpl<U>();
      } catch (...) {
        delete ptr;
        throw;
      }
      owner->vptr = &owner->vtable;
      owner->use_count = 1;
      owner->vtable.destroy = &shared_ptr_delete<U>;
      owner->ptr = ptr;

      m_c.owner = owner;
      m_c.ptr = ptr;
    }

    SharedPtr(const SharedPtr& src) {
      init(src.get(), src.m_c.owner);
    }

    template<typename U>
    SharedPtr(const SharedPtr<U>& src) {
      init(src.get(), src.m_c.owner);
    }

    template<typename U>
    SharedPtr(const SharedPtr<U>& src, T *ptr) {
      init(ptr, src.m_c.owner);
    }

    ~SharedPtr() {
      if (m_c.owner) {
        if (--m_c.owner->use_count == 0)
          m_c.owner->vptr->destroy(m_c.owner);
      }
    }
    
    T* get () const {return static_cast<T*>(m_c.ptr);}
    T& operator * () const {return *get();}
    T* operator -> () const {return get();}
    bool unique() const {return m_c.owner->use_count == 1;}

    SharedPtr& operator = (const SharedPtr& src) {
      SharedPtr<T>(src).swap(*this);
      return *this;
    }
    
    template<typename U>
    SharedPtr& operator = (const SharedPtr<U>& src) {
      SharedPtr<T>(src).swap(*this);
      return *this;
    }

    void swap(SharedPtr& other) {
      std::swap(m_c, other.m_c);
    }

    template<typename U>
    void reset(U *ptr) {
      SharedPtr<T>(ptr).swap(*this);
    }

    void reset() {
      SharedPtr<T>().swap(*this);
    }

    bool operator ! () const {return !get();}
    operator safe_bool_type () const {return get() ? &SharedPtr::safe_bool_true : 0;}
  };

  template<typename T, typename U>
  bool operator == (const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) {
    return lhs.get() == rhs.get();
  }

  template<typename T, typename U>
  bool operator != (const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) {
    return lhs.get() != rhs.get();
  }

  template<typename T, typename U>
  bool operator < (const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) {
    return lhs.get() < rhs.get();
  }

  template<typename T, typename U>
  SharedPtr<T> checked_pointer_cast(const SharedPtr<U>& src) {
    PSI_ASSERT(dynamic_cast<T*>(src.get()) == src.get());
    return SharedPtr<T>(src, static_cast<T*>(src.get()));
  }

  struct String_C {
    PsiSize length;
    char *data;
  };
  
  class PSI_COMPILER_COMMON_EXPORT String {
    String_C m_c;

    void init(const char*, PsiSize);
    
  public:
    String();
    String(const String&);
    String(const std::string&);
    String(const char*);
    String(const char*, const char*);
    String(const char*, PsiSize);
    ~String();
    
    String& operator = (const String&);
    String& operator = (const char*);

    bool operator == (const String&) const;
    bool operator != (const String&) const;
    bool operator < (const String&) const;
    operator std::string() const;

    void clear();
    std::size_t length() const {return m_c.length;}
    const char *c_str() const {return m_c.data;}
    bool empty() const {return !m_c.length;}
    void swap(String&);
    char operator [] (std::size_t idx) const {return m_c.data[idx];}
    
    friend std::size_t hash_value(const String& s) {
      if (s.empty())
        return 0;
      return boost::hash_range(s.c_str(), s.c_str() + s.length());
    }
  };
  
  PSI_COMPILER_COMMON_EXPORT bool operator == (const String& lhs, const char *rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const char *lhs, const String& rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const String& lhs, const std::string& rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const std::string& lhs, const String& rhs);
  
  PSI_VISIT_SIMPLE(String)

  PSI_COMPILER_COMMON_EXPORT std::ostream& operator << (std::ostream&, const String&);
  
  template<typename T>
  struct Maybe_C {
    PsiBool full;
    AlignedStorageFor<T> data;
  };
  
  template<typename T>
  class Maybe {
    Maybe_C<T> m_c;
    
    T* unchecked_get() {return m_c.data.ptr();}
    const T* unchecked_get() const {return m_c.data.ptr();}
    
  public:
    Maybe() {
      m_c.full = psi_false;
    }
    
    Maybe(const T& value) {
      m_c.full = psi_true;
      new (m_c.data) T (value);
    }
    
    Maybe(const Maybe<T>& other) {
      if (other) {
        m_c.full = psi_true;
        new (m_c.data) T (*other);
      } else {
        m_c.full = psi_false;
      }
    }
    
    ~Maybe() {
      if (m_c.full)
        get()->~T();
    }
    
    bool empty() const {return !m_c.full;}
    T* get() {return m_c.full ? unchecked_get() : 0;}
    const T* get() const {return m_c.full ? unchecked_get() : 0;}
    
    T* operator -> () {PSI_ASSERT(!empty()); return unchecked_get();}
    const T* operator -> () const {PSI_ASSERT(!empty()); return unchecked_get();}
    T& operator * () {PSI_ASSERT(!empty()); return *unchecked_get();}
    const T& operator * () const {PSI_ASSERT(!empty()); return *unchecked_get();}

    void clear() {
      if (m_c.full) {
        delete unchecked_get()->~T();
        m_c.full = psi_false;
      }
    }
    
    Maybe& operator = (const Maybe<T>& other) {
      if (other)
        operator = (*other);
    }
    
    Maybe& operator = (const T& src) {
      if (m_c.m_full)
        *unchecked_get() = src;
      else
        new (m_c.data) T (src);
    }
  };
  
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
    void assign(bool src);
    void assign(int src);
    void assign(double src);
    void assign(const PropertyMap& src);
    void assign(const PropertyList& src);
    
  public:
    PropertyValue() : m_type(t_null) {}
    PropertyValue(const PropertyValueNull&) : m_type(t_null) {}
    PropertyValue(const PropertyValue& src) : m_type(t_null) {assign(src);}
    PropertyValue(const std::string& src) : m_type(t_null) {assign(src);}
    PropertyValue(const String& src) : m_type(t_null) {assign(src);}
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

    PropertyList& list() {PSI_ASSERT(m_type == t_list); return *m_value.list.ptr();}
    const PropertyList& list() const {PSI_ASSERT(m_type == t_list); return *m_value.list.ptr();}
    std::vector<std::string> str_list() const;
    
    static PropertyValue parse(const char *begin, const char *end);
  };
  
  PSI_COMPILER_COMMON_EXPORT bool operator == (const PropertyValue& lhs, const PropertyValue& rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const PropertyValue& lhs, const String& rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const String& lhs, const PropertyValue& rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const PropertyValue& lhs, const char *rhs);
  PSI_COMPILER_COMMON_EXPORT bool operator == (const char *lhs, const PropertyValue& rhs);

  PSI_VISIT_SIMPLE(PropertyValue)

  enum LookupResultType {
    lookup_result_type_match, ///< \brief Match found
    lookup_result_type_none, ///< \brief No match found
    lookup_result_type_conflict ///< \brief Multiple ambiguous matches found
  };

  template<typename T>
  struct LookupResult_C {
    LookupResultType type;
    AlignedStorageFor<T> data;
  };

  struct LookupResultNoneHelper {};
  typedef int LookupResultNoneHelper::*LookupResultNone;
  struct LookupResultConflictHelper {};
  typedef int LookupResultConflictHelper::*LookupResultConflict;

  const LookupResultNone lookup_result_none = 0;
  const LookupResultConflict lookup_result_conflict = 0;

  template<typename T>
  class LookupResult {
    LookupResult_C<T> m_c;

  public:
    LookupResult(LookupResultNone) {
      m_c.type = lookup_result_type_none;
    }
    
    LookupResult(LookupResultConflict) {
      m_c.type = lookup_result_type_conflict;
    }
    
    LookupResult(const T& value) {
      m_c.type = lookup_result_type_match;
      new (m_c.data.ptr()) T(value);
    }

    LookupResult(const LookupResult& src) {
      m_c.type = src.type();
      if (m_c.type == lookup_result_type_match)
        new (m_c.data.ptr()) T(src.value());
    }
    
    template<typename U>
    LookupResult(const LookupResult<U>& src) {
      m_c.type = src.type();
      if (m_c.type == lookup_result_type_match)
        new (m_c.data.ptr()) T(src.value());
    }

    ~LookupResult() {
      clear();
    }

    LookupResultType type() const {
      return m_c.type;
    }

    const T& value() const {
      PSI_ASSERT(type() == lookup_result_type_match);
      return *m_c.data.ptr();
    }

    void clear() {
      if (m_c.type == lookup_result_type_match)
        m_c.data.ptr()->~T();
      m_c.type = lookup_result_type_none;
    }

    LookupResult& operator = (const LookupResult& src) {
      clear();
      m_c.type = src.m_c.type;
      if (m_c.type == lookup_result_type_match)
        new (m_c.data.ptr()) T(src.value());
      return *this;
    }
  };

  template<typename T>
  LookupResult<T> lookup_result_match(const T& value) {
    return LookupResult<T>(value);
  }
  
  PSI_COMPILER_COMMON_EXPORT std::vector<char> string_unescape(const std::vector<char>& s);
}

#endif
