#ifndef HPP_PSI_CPP_COMPILER
#define HPP_PSI_CPP_COMPILER

#ifdef __GNUC__
#define PSI_ALIGNOF(x) __alignof__(x)
#define PSI_ATTRIBUTE(x) __attribute__(x)
#define PSI_ALIGNED(x) aligned(x)
#define PSI_NORETURN noreturn
#if (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 5)
#define PSI_UNREACHABLE() __builtin_unreachable()
#else
#define PSI_UNREACHABLE() void()
#endif
#else
#error Unsupported compiler!
#define PSI_ATTRIBUTE(x)
#define PSI_NORETURN
#define PSI_UNREACHABLE() void()
#endif

#endif
