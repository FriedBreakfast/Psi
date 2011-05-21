#ifndef HPP_PSI_UTILITY
#define HPP_PSI_UTILITY

#include <cstddef>
#include <string>
#include <vector>

#include <boost/checked_delete.hpp>
#include <boost/functional/hash.hpp>
#include <boost/shared_ptr.hpp>

#include "Assert.hpp"
#include "Array.hpp"

namespace Psi {
  template<typename T>
  class PointerBase {
    typedef void (PointerBase::*safe_bool_type) () const;
    void safe_bool_true() const;
    
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
   * A simple empty type, implementing equality comparison and
   * hashing.
   */
  struct Empty {
    bool operator == (const Empty&) const {return true;}
    friend std::size_t hash_value(const Empty&) {return 0;}
  };
  
  /**
   * Base class which can be used to store empty types
   * cheaply. Currently this is implemented by specializing for Empty.
   */
  template<typename T>
  class CompressedBase {
  public:
    CompressedBase(const T& t) : m_value(t) {}
    T& get() {return m_value;}
    const T& get() const {return m_value;}

  private:
    T m_value;
  };

  template<>
  class CompressedBase<Empty> : Empty {
  public:
    CompressedBase(const Empty&) {}
    Empty& get() {return *this;}
    const Empty& get() const {return *this;}
  };

  /**
   * Wraps a primitive type to ensure it is initialized.
   */
  template<typename T>
  class PrimitiveWrapper {
  public:
    PrimitiveWrapper(T value) : m_value(value) {}

    T value() const {
      return m_value;
    }

    bool operator == (const PrimitiveWrapper<T>& other) const {
      return m_value == other.m_value;
    }

    friend std::size_t hash_value(const PrimitiveWrapper<T>& self) {
      return boost::hash_value(self.m_value);
    }

  private:
    T m_value;
  };
}

#endif
