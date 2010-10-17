#ifndef HPP_PSI_UTILITY
#define HPP_PSI_UTILITY

#include <cstddef>
#include <string>

#include <tr1/functional>

namespace Psi {
#ifdef PSI_DEBUG
#define PSI_ASSERT_MSG(cond,msg) (cond ? void() : Psi::assert_fail(#cond, msg))
#define PSI_ASSERT(cond) (cond ? void() : Psi::assert_fail(#cond, NULL))
#define PSI_FAIL(msg) (Psi::assert_fail(NULL, msg))
#else
#define PSI_ASSERT_MSG(cond,msg) void()
#define PSI_ASSERT(cond) void()
#define PSI_FAIL(msg) void()
#endif

#ifdef __GNUC__
#define PSI_ATTRIBUTE(x) __attribute__(x)
#define PSI_NORETURN noreturn
#define PSI_SENTINEL sentinel
#else
#define PSI_ATTRIBUTE(x)
#define PSI_NORETURN
#define PSI_SENTINEL
#endif

  void assert_fail(const char *test, const char *msg) PSI_ATTRIBUTE((PSI_NORETURN));

  /**
   * \brief Gets a pointer to a containing class from a pointer to a
   * member.
   *
   * Obviously, this requires that the pointer to the member was
   * obtained from the containing class in order for the result to be
   * valid.
   */
  template<typename T, typename U>
  T* reverse_member_lookup(U *member, U T::*member_ptr) {
    std::ptrdiff_t diff = reinterpret_cast<char*>(&(static_cast<T*>(NULL)->*member_ptr)) - static_cast<char*>(NULL);
    T *ptr = reinterpret_cast<T*>(reinterpret_cast<char*>(member) - diff);
    PSI_ASSERT(&(ptr->*member_ptr) == member);
    return ptr;
  }

  template<typename T>
  class UniquePtr {
    typedef void (UniquePtr::*SafeBoolType)() const;
    void safe_bool_true() const {}
  public:
    explicit UniquePtr(T *p=0) : m_p(p) {}
    ~UniquePtr() {delete m_p;}

    void reset(T *p=0) {
      delete m_p;
      m_p = p;
    }

    T* release() {
      T *p = m_p;
      m_p = 0;
      return p;
    }

    T* operator -> () const {return m_p;}
    T& operator * () const {return *m_p;}
    T* get() const {return m_p;}
    operator SafeBoolType () const {return m_p ? &UniquePtr::safe_bool_true : 0;}

    void swap(UniquePtr& o) {std::swap(m_p, o.m_p);}

  private:
    UniquePtr(const UniquePtr&);
    T *m_p;
  };

  template<typename T> void swap(UniquePtr<T>& a, UniquePtr<T>& b) {a.swap(b);}

  template<typename T>
  class UniqueArray {
    typedef void (UniqueArray::*SafeBoolType)() const;
    void safe_bool_true() const {}
  public:
    explicit UniqueArray(T *p=0) : m_p(p) {}
    ~UniqueArray() {delete [] m_p;}

    void reset(T *p=0) {
      delete [] m_p;
      m_p = p;
    }

    T* release() {
      T *p = m_p;
      m_p = 0;
      return p;
    }

    T& operator [] (const std::size_t i) {return m_p[i];}
    T* get() const {return m_p;}
    operator SafeBoolType () const {return m_p ? &UniqueArray::safe_bool_true : 0;}

    void swap(UniqueArray& o) {std::swap(m_p, o.m_p);}

  private:
    UniqueArray(const UniqueArray&);
    T *m_p;
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
}

#endif
