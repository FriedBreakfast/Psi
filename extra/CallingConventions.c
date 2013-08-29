/**
 * CallingConvetions.c
 * 
 * Contains prototypes of a bunch of different functions to see how certain types
 * and combinations of parameters are passed, either by running the native
 * compiler on this file and examining the resulting assembler or by running clang
 * and examining the resulting bitcode.
 * 
 * Example commands:
 * 
 * gcc -S -o- CallingConventions.c
 * clang -emit-llvm -o- -S CallingConventions.c
 * clang -ccc-host-triple i386-pc-linux-gnu -emit-llvm -o- -S CallingConventions.c
 */

// Should work for most platforms...
typedef long long int64_t;
typedef int int32_t;

#include <complex.h>

#ifdef CALLING_CONVENTION
#define CC __attribute__((CALLING_CONVENTION))
#else
#define CC
#endif

typedef struct Char4 {
  char x[4];
} Char4;

CC Char4 char4_1(Char4 a) {
  return a;
}

typedef struct Char4_Alt {
  char a, b, c, d;
} Char4_Alt;

CC Char4_Alt char4alt_1(Char4_Alt a) {
  return a;
}

typedef struct Char8 {
  char x[8];
} Char8;

CC Char8 char8_1(Char8 a) {
  return a;
}

typedef struct Char12 {
  char x[12];
} Char12;

CC Char12 char12_1(Char12 a) {
  return a;
}

typedef struct Long2 {
  int64_t a, b;
} Long2;

CC Long2 long2_1(Long2 a) {
  return a;
}

CC Long2 long2_2(Long2 a, Long2 b) {
  return b;
}

CC Long2 long2_3(Long2 a, Long2 b, Long2 c) {
  return c;
}

CC Long2 long2_4(Long2 a, Long2 b, Long2 c, Long2 d) {
  return d;
}

typedef struct Long3 {
  int64_t a, b, c;
} Long3;

CC Long3 long3_1(Long3 a) {
  return a;
}

typedef struct Mixed2 {
  int64_t a;
  int32_t b, c;
} Mixed2;

CC Mixed2 mixed2_1(Mixed2 a) {
  return a;
}

typedef struct LongFloatMix {
  int64_t a;
  double b;
} LongFloatMix;

CC LongFloatMix long_float_mix(LongFloatMix a) {
  return a;
}

#if defined(__GNUC__) && !defined(__clang__)
typedef union Float128Union {
  __float128 a;
  int64_t b;
} Float128Union;

// Checks that the AMD64 calling convention doc, which implies that
// the first word of a will be passed in an integer register and
// the second word in an SSE register, is correct.
CC Float128Union float_union(Float128Union a) {
  return a;
}
#endif

long double ldbl(long double a, long double b, long double c) {
  return a;
}

long double complex ldbl_complex(long double complex a, long double complex b, long double complex c) {
  return a;
}

typedef union LongDoubleUnion {
  long double a;
  int64_t b;
} LongDoubleUnion;

CC LongDoubleUnion ldbl_union(LongDoubleUnion a) {
  return a;
}

typedef union IntUnion {
  int32_t a[2];
  int64_t b;
} IntUnion;

CC IntUnion int_union(IntUnion a) {
  return a;
}