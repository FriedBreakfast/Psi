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

  PSI_COMPILER_COMMON_EXPORT void unicode_encode(std::vector<char>& output, unsigned value);
  PSI_COMPILER_COMMON_EXPORT std::string string_escape(const std::string& s);
  PSI_COMPILER_COMMON_EXPORT std::vector<char> string_unescape(const std::vector<char>& s);
}

#endif
