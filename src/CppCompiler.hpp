#ifndef HPP_PSI_CPP_COMPILER
#define HPP_PSI_CPP_COMPILER

namespace Psi {
#ifdef __GNUC__
#define PSI_STD std
#define PSI_STDINC ""
#define PSI_ALIGNOF(x) __alignof__(x)
#define PSI_ATTRIBUTE(x) __attribute__(x)
#define PSI_ALIGNED(x) aligned(x)
#define PSI_ALIGNED_MAX aligned(__BIGGEST_ALIGNMENT__)
#define PSI_NORETURN noreturn
#define PSI_PURE pure
#define PSI_UNUSED unused

  struct DebugLocation {
    const char *file;
    int line;
    const char *function;

    DebugLocation(const char *file_, int line_, const char *function_)
      : file(file_), line(line_), function(function_) {}
  };
#define PSI_DEBUG_LOCATION() DebugLocation(__FILE__, __LINE__, __PRETTY_FUNCTION__)

#if (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 5)
#define PSI_UNREACHABLE() __builtin_unreachable()
#else
#define PSI_UNREACHABLE() void()
#endif
#else
#error Unsupported compiler!
#define PSI_UNUSED(x)
#define PSI_STD std
#define PSI_STDINC ""
#define PSI_ATTRIBUTE(x)
#define PSI_NORETURN
#define PSI_PURE
#define PSI_UNUSED
#define PSI_UNREACHABLE() void()

  struct DebugLocation {
    const char *file;
    int line;

    DebugLocation(const char *file_, int line_)
      : file(file_), line(line_) {}
  };
#define PSI_DEBUG_LOCATION() DebugLocation(__FILE__, __LINE__)

#endif
}

#endif