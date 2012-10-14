#ifndef PSI_COMPILER_GCCHECKER
#define PSI_COMPILER_GCCHECKER

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct psi_gcchecker_block_s {
  void *base;
  size_t size;
} psi_gcchecker_block;

#ifdef __cplusplus
}
#endif

#endif
