#ifndef HPP_PSI_UTILITY
#define HPP_PSI_UTILITY

#include <cstddef>
#include <string>
#include <cstring>
#include <vector>

#include <boost/aligned_storage.hpp>
#include <boost/functional/hash.hpp>
#include <boost/shared_ptr.hpp>

#include "Assert.hpp"
#include "Array.hpp"

namespace Psi {
  template<bool v> struct static_bool {static const bool value = v;};
  
  /**
   * \brief Base class for types which should never be constructed.
   */
  class NonConstructible {
    NonConstructible();
  };

  /**
   * \brief Allows easy access to default constructors without writing
   * out entire types.
   */
  struct DefaultConstructor {
    template<typename T> operator T () {
      return T();
    }
  };

  namespace {
    DefaultConstructor default_ PSI_ATTRIBUTE((PSI_UNUSED_ATTR)) = {};
  };
  
  template<typename T>
  struct AlignedStorageFor {
    typedef typename boost::aligned_storage<sizeof(T), boost::alignment_of<T>::value>::type type;
    type data;
    T *ptr() {return static_cast<T*>(static_cast<void*>(&data));}
    const T *ptr() const {return static_cast<const T*>(static_cast<const void*>(&data));}
  };

  /**
   * \brief Used to wrap C functions where a C++ class is constructed into a passed pointer.
   */
  template<typename T>
  class ResultStorage : boost::noncopyable {
    bool m_constructed;
    AlignedStorageFor<T> m_data;

  public:
    ResultStorage() : m_constructed(false) {}
    ~ResultStorage() {if (m_constructed) m_data.ptr()->~T();}
    T *ptr() {return m_data.ptr();}
    T& done() {m_constructed = true; return *m_data.ptr();}
  };

  template<typename T>
  class PointerBase {
    typedef void (PointerBase::*safe_bool_type) () const;
    void safe_bool_true() const {}

    PointerBase(const PointerBase&);
    PointerBase& operator = (const PointerBase&);
    
  protected:
    T *m_ptr;

    PointerBase() : m_ptr(0) {}
    PointerBase(T *p) : m_ptr(p) {}

  public:
    T* get() const {return m_ptr;}
    T* operator -> () const {return get();}
    T& operator * () const {return *get();}
    operator safe_bool_type () const {return get() ? &PointerBase::safe_bool_true : 0;}
    bool operator ! () const {return !get();}
  };

  template<typename T, typename U>
  bool operator == (const PointerBase<T>& lhs, const PointerBase<U>& rhs) {
    return lhs.get() == rhs.get();
  }

  template<typename T, typename U>
  bool operator != (const PointerBase<T>& lhs, const PointerBase<U>& rhs) {
    return lhs.get() != rhs.get();
  }

  template<typename T, typename U>
  bool operator < (const PointerBase<T>& lhs, const PointerBase<U>& rhs) {
    return lhs.get() < rhs.get();
  }

  template<typename T>
  class IntrusivePointer : public PointerBase<T> {
  public:
    IntrusivePointer() {}
    explicit IntrusivePointer(T *ptr) {reset(ptr);}
    IntrusivePointer(T *ptr, bool add_ref) {reset(ptr, add_ref);}
    IntrusivePointer(const IntrusivePointer& src) : PointerBase<T>() {reset(src.get());}
    template<typename U> IntrusivePointer(const IntrusivePointer<U>& src) {reset(src.get());}
    ~IntrusivePointer() {reset();}

    T* release() {
      T *ptr = this->m_ptr;
      this->m_ptr = 0;
      return ptr;
    }

    void reset(T *ptr=0, bool add_ref=true) {
      if (ptr && add_ref)
	intrusive_ptr_add_ref(ptr);

      if (this->m_ptr)
	intrusive_ptr_release(this->m_ptr);

      this->m_ptr = ptr;
    }

    IntrusivePointer& operator = (const IntrusivePointer& src) {
      reset(src.get());
      return *this;
    }
  };
  
  template<typename T>
  class UniquePtr : public PointerBase<T> {
    void clear() {
      if (this->m_ptr)
        delete this->m_ptr;
    }

  public:
    template<typename> friend class UniquePtr;

    UniquePtr() {}
    explicit UniquePtr(T *ptr) : PointerBase<T>(ptr) {}
    ~UniquePtr() {clear();}

    void reset(T *ptr=0) {
      clear();
      this->m_ptr = ptr;
    }

    T* release() {
      T *p = this->m_ptr;
      this->m_ptr = 0;
      return p;
    }

    void swap(UniquePtr<T>& src) {
      std::swap(this->m_ptr, src.m_ptr);
    }
  };

  template<typename T, typename U>
  struct checked_cast_impl;

  template<typename T, typename U>
  struct checked_cast_impl<T&,U&> {
    static T& cast(U& src) {
      PSI_ASSERT(&src == dynamic_cast<T*>(&src));
      return static_cast<T&>(src);
    }
  };

  template<typename T, typename U>
  struct checked_cast_impl<T*,U*> {
    static T* cast(U* src) {
      PSI_ASSERT(src == dynamic_cast<T*>(src));
      return static_cast<T*>(src);
    }
  };

  /**
   * \brief Checked cast function.
   *
   * This follows the design of boost::polymorphic downcast with two exceptions:
   *
   * <ul>
   * <li>It supports casts involving references</li>
   * <li>It uses PSI_ASSERT() rather than assert() to check for
   * validity so leaves defining assert() up to the user rather than
   * possibly violating the ODR.</li>
   * </ul>
   */
  template<typename T, typename U>
  T checked_cast(U& src) {
    return checked_cast_impl<T,U&>::cast(src);
  }

  template<typename T, typename U>
  T checked_cast(U* src) {
    return checked_cast_impl<T,U*>::cast(src);
  }

  /**
   * checked_pointer_cast implementation for boost::shared_ptr.
   */
  template<typename T, typename U>
  boost::shared_ptr<T> checked_pointer_cast(const boost::shared_ptr<U>& ptr) {
    PSI_ASSERT(ptr.get() == dynamic_cast<T*>(ptr.get()));
    return boost::static_pointer_cast<T>(ptr);
  }

  /**
   * A base class for types which want to work with checked_cast but
   * would not ordinarily have any virtual members (and hence RTTI
   * would not be available). This class defines a virtual destructor
   * if PSI_DEBUG is defined, so checked_cast will be able to verify
   * casts.
   */
  struct CheckedCastBase {
#ifdef PSI_DEBUG
    virtual ~CheckedCastBase();
#endif
  };

  /**
   * \brief Does nothing.
   *
   * This can be used to require the existence of a conversion to type T.
   */
  template<typename T> void no_op(const T&) {}

  /**
   * \brief Check value is convertible to type.
   */
#define PSI_REQUIRE_CONVERTIBLE(value,type) (false ? no_op<type>(value) : void())

  /// \brief isdigit() fixed to the C locale.
  inline bool c_isdigit(char c) {
    return (c >= '0') && (c <= '9');
  }
  
  /// \brief isalpha() fixed to the C locale.
  inline bool c_isalpha(char c) {
    return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
  }
  
  /// \brief isalnum() fixed to the C locale.
  inline bool c_isalnum(char c) {
    return c_isdigit(c) || c_isalpha(c);
  }

}

#endif
