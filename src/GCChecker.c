/**
 * \file
 * 
 * Used to debug garbage collection. This is done (on Linux) by overriding
 * malloc() and free() to allow tracing memory allocation, then we scan
 * allocated blocks to produce an object graph.
 * 
 * I haven't tried to make this either fast or robust; it's strictly for
 * testing. Overriding memory allocation functions in this way seems to
 * be rather subtle, for example I tried linking against libdl explicitly
 * which caused an infinte loop crash because dlerror was calling malloc().
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <execinfo.h>

#include "GCChecker.h"

typedef struct block_header_s {
  struct block_header_s *prev, *next;
  psi_gcchecker_block info;
} block_header;

static block_header block_root = {&block_root, &block_root, NULL, 0};
static pthread_mutex_t lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static int backtrace_block_size = -1;
static int hook_disable = 0;
static psi_gcchecker_hook_type free_hook = NULL;
static void *free_hook_ptr = NULL;

static void block_tree_insert(block_header *node) {
  node->next = &block_root;
  node->prev = block_root.prev;
  node->next->prev = node;
  node->prev->next = node;
}

static void block_tree_erase(block_header *node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;
}

// This needs to be fixed to the maximum required variable alignment for the local machine.
#define MAX_VARIABLE_ALIGN 16
#define BLOCK_HEADER_OFFSET ((sizeof(block_header) + MAX_VARIABLE_ALIGN - 1) & ~(MAX_VARIABLE_ALIGN - 1))

static void* setup_block(void *base_ptr, size_t size) {
  void *user_ptr = ((char*)base_ptr) + BLOCK_HEADER_OFFSET;

  block_header *hdr = (block_header*)base_ptr;
  hdr->info.size = size;
  hdr->info.base = user_ptr;

  pthread_mutex_lock(&lock);
  
  block_tree_insert(hdr);
  
  if (backtrace_block_size == -1) {
    const char *sz = getenv("PSI_GC_SIZE");
    if (sz)
      backtrace_block_size = atoi(sz);
    else
      backtrace_block_size = 0;
  }
    
  pthread_mutex_unlock(&lock);

  if (size == backtrace_block_size)
    backtrace(hdr->info.backtrace, PSI_GCCHECKER_BACKTRACE_COUNT);
  
  return user_ptr;
}

static block_header* teardown_block(void *ptr) {
  void *base_ptr = ((char*)ptr) - BLOCK_HEADER_OFFSET;
  block_header *hdr = (block_header*)base_ptr;

  pthread_mutex_lock(&lock);
  block_tree_erase(hdr);
  pthread_mutex_unlock(&lock);

  return hdr;
}

void* malloc(size_t size) {
  static void* (*libc_calloc) (size_t,size_t) = NULL;
  if (!libc_calloc)
    *(void**)&libc_calloc = dlsym(RTLD_NEXT, "calloc");

  void *base_ptr = libc_calloc(1, size + BLOCK_HEADER_OFFSET);
  return setup_block(base_ptr, size);
}

void* calloc(size_t nmemb, size_t size) {
  return malloc(nmemb*size);
}

void free(void *ptr) {
  static void* (*libc_free) (void*) = NULL;
  if (!libc_free)
    *(void**)&libc_free = dlsym(RTLD_NEXT, "free");

  if (ptr) {
    block_header *head = teardown_block(ptr);
    if (!hook_disable && free_hook) {
      hook_disable = 1;
      free_hook(head->info.base, head->info.size, free_hook_ptr);
      hook_disable = 0;
    }
    libc_free(head);
  }
}

void* realloc(void *ptr, size_t size) {
  if (!ptr) {
    return malloc(size);
  } else if (!size) {
    free(ptr);
    return NULL;
  }

  static void* (*libc_realloc) (void*,size_t) = NULL;
  if (!libc_realloc)
    *(void**)&libc_realloc = dlsym(RTLD_NEXT, "realloc");

  block_header *hdr = teardown_block(ptr);
  
  void *new_base_ptr = libc_realloc(hdr, size + BLOCK_HEADER_OFFSET);
  if (!new_base_ptr) {
    setup_block(hdr, size);
    return NULL;
  }
  
  return setup_block(new_base_ptr, size);
}

/**
 * \brief Get the allocated block list.
 */
size_t psi_gcchecker_blocks(psi_gcchecker_block **ptr) {
  pthread_mutex_lock(&lock);
  size_t n = 0;
  block_header *p;
  // Count number of blocks
  for (p = block_root.next; p != &block_root; p = p->next, ++n);
  
  psi_gcchecker_block *list = (psi_gcchecker_block*)malloc(sizeof(psi_gcchecker_block) * n);
  psi_gcchecker_block *list_p = list;
  *ptr = list;
  
  size_t c = 0;
  for (p = block_root.next; p != &block_root; p = p->next, ++list_p) {
    if (p->info.base != list)
      *list_p = p->info;
  }
  
  pthread_mutex_unlock(&lock);
  
  
  return n;
}

void psi_gcchecker_set_free_hook(psi_gcchecker_hook_type hook, void *ptr) {
  free_hook = hook;
  free_hook_ptr = ptr;
}
