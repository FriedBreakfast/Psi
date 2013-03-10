#ifndef PSI_COMPILER_GCCHECKER
#define PSI_COMPILER_GCCHECKER

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSI_GCCHECKER_BACKTRACE_COUNT 10

typedef void (*psi_gcchecker_hook_type) (void*,size_t,void*);

typedef struct psi_gcchecker_block_s {
  void *base;
  size_t size;
  void *backtrace[PSI_GCCHECKER_BACKTRACE_COUNT];
} psi_gcchecker_block;

#ifdef __cplusplus
}
#endif

#endif
