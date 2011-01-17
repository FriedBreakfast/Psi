#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

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

#endif
