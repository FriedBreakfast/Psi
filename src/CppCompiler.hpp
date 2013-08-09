#ifndef HPP_PSI_CPP_COMPILER
#define HPP_PSI_CPP_COMPILER

#include <cstddef>

namespace Psi {
#if defined(__GNUC__)
#define PSI_STD std
#define PSI_STDINC ""
#define PSI_ALIGNOF(x) __alignof__(x)
#define PSI_ATTRIBUTE(x) __attribute__(x)
#define PSI_ALIGNED(x) aligned(x)
#define PSI_ALIGNED_MAX aligned(__BIGGEST_ALIGNMENT__)
#define PSI_NORETURN noreturn
#define PSI_UNUSED_ATTR unused
#define PSI_SMALL_ENUM(name) enum __attribute__((packed)) name
#define PSI_ASSUME(x) void()
#define PSI_THREAD_LOCAL __thread
#define PSI_FLEXIBLE_ARRAY(decl) decl[0]

#ifdef _WIN32
#define PSI_EXPORT dllexport
#define PSI_IMPORT dllimport
#define PSI_LOCAL_ATTR
#else
#define PSI_EXPORT visibility("default")
#define PSI_IMPORT visibility("default")
#define PSI_LOCAL_ATTR visibility("hidden")
#endif

  struct DebugLocation {
    const char *file;
    int line;
    const char *function;

    DebugLocation(const char *file_, int line_, const char *function_)
      : file(file_), line(line_), function(function_) {}
  };
#define PSI_DEBUG_LOCATION() ::Psi::DebugLocation(__FILE__, __LINE__, __PRETTY_FUNCTION__)

#if (__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5))
#define PSI_UNREACHABLE() __builtin_unreachable()
#else
#define PSI_UNREACHABLE() void()
#endif
  
#if defined(__MINGW32__) && !defined(__MINGW64__)
  typedef long AtomicCount;
  extern "C" __stdcall long InterlockedIncrement(long volatile*);
  extern "C" __stdcall long InterlockedDecrement(long volatile*);
  inline long atomic_increment(long& x) {return InterlockedIncrement(&x);}
  inline long atomic_decrement(long& x) {return InterlockedDecrement(&x);}
#else
  typedef std::size_t AtomicCount;
  inline std::size_t atomic_increment(std::size_t& x) {return __sync_add_and_fetch(&x, 1);}
  inline std::size_t atomic_decrement(std::size_t& x) {return __sync_sub_and_fetch(&x, 1);}
#endif
  
#elif defined(_MSC_VER)
#define PSI_STD std
#define PSI_STDINC ""
#define PSI_ALIGNOF(x) __alignof(x)
#define PSI_ATTRIBUTE(x) __declspec x
#define PSI_NORETURN noreturn
#define PSI_UNUSED_ATTR
#define PSI_ALIGNED(x) align(x)
#define PSI_ALIGNED_MAX align(16)
#define PSI_UNREACHABLE() __assume(false)
#define PSI_ASSUME(x) __assume(x)
#define PSI_THREAD_LOCAL __declspec(thread)
#define PSI_FLEXIBLE_ARRAY(decl) __pragma(warning(push)) __pragma(warning(disable:4200)); decl[]; __pragma(warning(pop))
#define PSI_SMALL_ENUM(name) enum name : unsigned char
#define PSI_EXPORT dllexport
#define PSI_IMPORT dllimport
#define PSI_LOCAL_ATTR

  struct DebugLocation {
    const char *file;
    int line;
    const char *function;

    DebugLocation(const char *file_, int line_, const char *function_)
      : file(file_), line(line_), function(function_) {}
  };

#define PSI_DEBUG_LOCATION() ::Psi::DebugLocation(__FILE__, __LINE__, __FUNCTION__)
  
  typedef std::ptrdiff_t AtomicCount;
  inline long atomic_increment(long& x) {return _InterlockedIncrement(&x);}
  inline long atomic_decrement(long& x) {return _InterlockedDecrement(&x);}
  inline __int64 atomic_increment(__int64& x) {return _InterlockedIncrement64(&x);}
  inline __int64 atomic_decrement(__int64& x) {return _InterlockedDecrement64(&x);}
  
#else
#error Unsupported compiler!
#define PSI_STD std
#define PSI_STDINC ""
#define PSI_ATTRIBUTE(x)
#define PSI_NORETURN
#define PSI_UNUSED_ATTR
#define PSI_UNREACHABLE() void()
  /**
   * \brief Assume that a condition is true.
   * 
   * The expression \c x is not evaluated, this merely informs the optimizer what
   * to expect were the expression evaluated.
   */
#define PSI_ASSUME(x) void()

  /**
   * \brief Create an enumeration which only takes one byte.
   */
#define PSI_SMALL_ENUM(name) typedef unsigned char name; enum name##_enum_values

  struct DebugLocation {
    const char *file;
    int line;

    DebugLocation(const char *file_, int line_)
      : file(file_), line(line_) {}
  };
#define PSI_DEBUG_LOCATION() DebugLocation(__FILE__, __LINE__)
  
  /// \brief Increment a value atomically, and return the new value.
  std::size_t atomic_increment(std::size_t& x);
#endif

#ifdef PSI_DOXYGEN
#define PSI_UNUSED(x) x
#else
#define PSI_UNUSED(x)
#endif
}

#endif
