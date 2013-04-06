#include <signal.h>
#include <stdlib.h>

/**
 * \brief Stack allocation routine.
 * 
 * This is used when a requested stack allocation is too large, then code is generated to perform
 * heap allocation instead. This function is called to perform that heap allocation.
 * 
 * Memory must be allocated and freed in order. That is, memory allocated by a given __psi_alloca() must
 * be freed before freeing memory allocated by a __psi_alloca() call prior to that one. Obviously this applies
 * on a per-thread basis to support multithreaded environments.
 * This is to allow a low overhead implementation using a linked sequence of blocks.
 * 
 * Currently these functions are implemented using malloc() and free(), and abort() is called if malloc() fails.
 * 
 * \param count Number of bytes to allocate.
 * \param align Minimum alignment of the returned pointer. Note that only alignments up to the largest required
 * alignment on the current platform are supported (and this must be a power of two), i.e. the largest alignment
 * that may be specified is that used by the system malloc().
 * 
 * \return Pointer to allocated memory. NULL is never returned. The contents of returned memory are undefined.
 * 
 * \todo Raise an exception when allocation fails.
 */
void* __psi_alloca(size_t count, size_t align) {
  void *p = malloc(count);
  if (!p) {
    raise(SIGSEGV);
    // raise should not return
    abort();
  }
  return p;
}

/**
 * \brief Free memory allocated by __psi_alloca().
 * 
 * The \c count and \c align parameters must be the same as those passed to the corresponding __psi_alloca() call.
 * 
 * \param ptr Pointer returned by by __psi_alloca(). Unlike free(), this pointer
 * may not be NULL.
 * 
 * See __psi_alloca() for usage details.
 */
void __psi_freea(void *ptr, size_t count, size_t align) {
  free(ptr);
}
