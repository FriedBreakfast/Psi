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

#if __cplusplus > 199711L
#define PSI_STATIC_ASSERT(cond) static_assert(cond)
#define PSI_STATIC_ASSERT_MSG(cond,msg) static_assert(cond,msg)
#else
  template<bool> struct StaticAssert;
  template<> struct StaticAssert<true> {static const int value=0;};
  template<int> struct StaticAssertType;

#define PSI_STATIC_ASSERT(cond) typedef StaticAssertType<StaticAssert<(cond)>::value> PSI_STATIC_ASSERT_ ## __LINE__
#define PSI_STATIC_ASSERT_MSG(cond,msg) PSI_STATIC_ASSERT(cond)
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

  template<typename T>
  struct AlignOf {
#if __cplusplus > 199711L
    static const std::size_t value = alignof(T);
#elif defined(__GNUC__)
    static const std::size_t value = __alignof__(T);
#else
  private:
    struct Test {
      char c;
      T t;
    };

  public:
    static const std::size_t value = sizeof(Test) - sizeof(T);
#endif
    PSI_STATIC_ASSERT_MSG((value > 0) && ((value & (value - 1)) == 0),
			  "type alignment is not a power of two");
  };

  /**
   * \brief rvalue version of AlignOf.
   *
   * If you see an error like
   * <tt>Psi::AlignOf&lt;short&gt;::value</tt>, use
   * <tt>align_of&lt;T&gt;()</tt> instead. The reason for this is that
   * <tt>AlignOf::value</tt> is an lvalue so when passed as a <tt>const
   * std::size_t&</tt> to a function its address is required.
   */
  template<typename T> std::size_t align_of() {return AlignOf<T>::value;}

  void assert_fail(const char *test, const char *msg) PSI_ATTRIBUTE((PSI_NORETURN));

  class Noncopyable {
  public:
    Noncopyable() {}
  private:
    Noncopyable(const Noncopyable&);
  };

  /**
   * Base class which complements #checked_pointer_static_cast by
   * including a virtual destructor only if \c NDEBUG is not
   * defined. Like #checked_static_pointer_cast, this will violate the
   * ODR if \c NDEBUG is not consistently defined.
   */
  class CheckedCastBase {
  public:
#ifdef PSI_DEBUG
    virtual ~CheckedCastBase();
#endif
  };

  /**
   * A \c static_cast which is checked to be correct via \c
   * dynamic_cast using <tt>assert()</tt>. \c static_cast should
   * apparently behave correctly on NULL pointers according to the
   * standard (5.2.9/8).
   */
  template<typename T, typename U>
  T* checked_pointer_static_cast(U *ptr) {
    PSI_ASSERT(dynamic_cast<T*>(ptr) == ptr);
    return static_cast<T*>(ptr);
  }

  /**
   * A \c static_cast which is checked to be correct via \c
   * dynamic_cast using <tt>assert()</tt>. \c static_cast should
   * apparently behave correctly on NULL pointers according to the
   * standard (5.2.9/8).
   */
  template<typename T, typename U>
  T& checked_reference_static_cast(U& ref) {
    PSI_ASSERT(dynamic_cast<T*>(&ref) == &ref);
    return static_cast<T&>(ref);
  }

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
  class UniquePtr : Noncopyable {
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
    T *m_p;
  };

  template<typename T> void swap(UniquePtr<T>& a, UniquePtr<T>& b) {a.swap(b);}

  template<typename T>
  class UniqueArray : Noncopyable {
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
    T *m_p;
  };

#if 0
  namespace Format {
    template<typename Formatter>
    void format_insert(Formatter&) {
    }

    template<typename Formatter, typename T, typename... Args>
    void format_insert(Formatter& fmt, T&& first, Args&&... args) {
      fmt % std::forward<T>(first);
      format_insert(fmt, std::forward<Args>(args)...);
    }
  }

  template<typename... Args>
  std::string format(const char *fmt, Args&&... args) {
    boost::basic_format<char> formatter(fmt);
    Format::format_insert(formatter, std::forward<Args>(args)...);
    return formatter.str();
  }

  template<typename... Args>
  std::string format(const std::string& fmt, Args&&... args) {
    return format(fmt.c_str(), std::forward<Args>(args)...);
  }
#endif
}

#endif
