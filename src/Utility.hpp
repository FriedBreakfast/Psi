#ifndef HPP_PSI_UTILITY
#define HPP_PSI_UTILITY

#include <cstddef>
#include <string>

#include <tr1/functional>

#include <boost/checked_delete.hpp>
#include <boost/functional/hash.hpp>
#include <boost/intrusive/list.hpp>

namespace Psi {
#ifdef __GNUC__
#define PSI_ATTRIBUTE(x) __attribute__(x)
#define PSI_NORETURN noreturn
#if (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 5)
#define PSI_UNREACHABLE() __builtin_unreachable()
#else
#define PSI_UNREACHABLE() void()
#endif
#else
#define PSI_ATTRIBUTE(x)
#define PSI_NORETURN
#define PSI_UNREACHABLE() void()
#endif

#ifdef PSI_DEBUG
#define PSI_ASSERT_MSG(cond,msg) ((cond) ? void() : Psi::assert_fail(#cond, msg))
#define PSI_ASSERT(cond) ((cond) ? void() : Psi::assert_fail(#cond, NULL))
#define PSI_FAIL(msg) (Psi::assert_fail(NULL, msg))
  /**
   * \brief Issue a warning. This should be used in destructors where
   * PSI_ASSERT causes confusion in debugging.
   */
#define PSI_WARNING(cond) (cond ? void() : Psi::warning_fail(#cond, NULL))

  void assert_fail(const char *test, const char *msg) PSI_ATTRIBUTE((PSI_NORETURN));
  void warning_fail(const char *test, const char *msg);
#else
#define PSI_ASSERT_MSG(cond,msg) void()
#define PSI_ASSERT(cond) void()
#define PSI_FAIL(msg) PSI_UNREACHABLE()
#define PSI_WARNING(cond) void()
#endif

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

  template<typename ForwardIterator, typename Compare>
  bool is_sorted(ForwardIterator first, ForwardIterator last, Compare cmp) {
    if (first == last)
      return true;

    ForwardIterator next = first;
    for (++next; next != last; first = next, ++next) {
      if (!cmp(*first, *next))
        return false;
    }
  
    return true;
  }

  template<typename ForwardIterator, typename Compare>
  bool is_sorted(ForwardIterator first, ForwardIterator last) {
    return is_sorted(first, last, std::less<typename ForwardIterator::value_type>());
  }

  /**
   * \brief Array reference which also stores a length.
   *
   * This allows a pointer and a length to be passed around easily,
   * but does not perform any memory management.
   */
  template<typename T>
  class ArrayPtr {
  public:
    ArrayPtr() : m_ptr(0), m_size(0) {}
    ArrayPtr(T *ptr, std::size_t size) : m_ptr(ptr), m_size(size) {}
    template<typename U> ArrayPtr(ArrayPtr<U> src) : m_ptr(src.get()), m_size(src.size()) {}

    T* get() const {return m_ptr;}
    T& operator [] (std::size_t n) const {PSI_ASSERT(n < m_size); return m_ptr[n];}
    std::size_t size() const {return m_size;}

    ArrayPtr<T> slice(std::size_t start, std::size_t end) {
      PSI_ASSERT((start <= end) && (end <= m_size));
      return ArrayPtr<T>(m_ptr+start, end-start);
    }

  private:
    T *m_ptr;
    std::size_t m_size;
  };

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

  /**
   * Version of boost::intrusive::list which deletes all owned objects
   * on destruction.
   */
  template<typename T>
  class UniqueList : public boost::intrusive::list<T> {
  public:
    ~UniqueList() {
      clear_and_dispose(boost::checked_deleter<T>());
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
   * An adapter class used to make a static class look like a
   * pointer. The star and arrow operators return a pointer to an
   * internal structure which provides custom adapter functions.
   *
   * \tparam T Wrapper type. This should have a member \c GetType
   * which is the type returned by the get function (without the
   * pointer). This type should be copy-constructible.
   */
  template<typename T>
  class PtrAdapter {
    typedef void (PtrAdapter::*SafeBoolType)() const;
    void safe_bool_true() const {}
  public:
    typedef PtrAdapter<T> ThisType;
    typedef T WrapperType;
    typedef typename T::GetType GetType;

    PtrAdapter() {}
    PtrAdapter(const WrapperType& wrapper) : m_wrapper(wrapper) {}

    GetType* get() const {return m_wrapper.get();}
    const WrapperType& operator * () const {return m_wrapper;}
    const WrapperType* operator -> () const {return &m_wrapper;}

    bool operator ! () const {return !get();}
    bool operator == (const GetType *ptr) const {return ptr == get();}
    bool operator != (const GetType *ptr) const {return ptr != get();}
    bool operator < (const GetType *ptr) const {return ptr < get();}
    bool operator == (const ThisType& ptr) const {return get() == ptr.get();}
    bool operator != (const ThisType& ptr) const {return get() != ptr.get();}
    bool operator < (const ThisType& ptr) const {return get() < ptr.get();}

  private:
    WrapperType m_wrapper;
  };

  /**
   * Derived from PtrAdapter - also includes an implicit cast to
   * pointer operation.
   */
  template<typename T>
  class PtrDecayAdapter : public PtrAdapter<T> {
  public:
    PtrDecayAdapter() {}
    PtrDecayAdapter(const T& wrapper) : PtrAdapter<T>(wrapper) {}

    operator typename T::GetType* () const {return this->get();}
  };

  /**
   * \brief Base class for types which should never be constructed.
   */
  class NonConstructible {
  private:
    NonConstructible();
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
