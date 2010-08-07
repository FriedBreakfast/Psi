#ifndef HPP_PSI_UTILITY
#define HPP_PSI_UTILITY

#include <cstddef>
#include <string>

#include <boost/format.hpp>

namespace Psi {
#ifdef PSI_DEBUG
#define PSI_ASSERT(cond,msg) (cond ? Psi::assert_fail(#cond, msg) : void())
#else
#define PSI_ASSERT(cond,msg)
#endif

#define PSI_STATIC_ASSERT(cond,msg) static_assert(cond,msg)

#ifdef __GNUC__
#define PSI_ATTRIBUTE(x) __attribute__(x)
#define PSI_NORETURN noreturn
#else
#define PSI_ATTRIBUTE(x)
#define PSI_NORETURN
#endif

  void assert_fail(const char *test, const std::string& msg) PSI_ATTRIBUTE((PSI_NORETURN));

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
    PSI_ASSERT(dynamic_cast<T*>(ptr) == ptr, "Static cast failed");
    return static_cast<T*>(ptr);
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
    PSI_ASSERT(&(ptr->*member_ptr) == member, "reverse_member_lookup pointer arithmetic is incorrect");
    return ptr;
  }

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
}

#endif
