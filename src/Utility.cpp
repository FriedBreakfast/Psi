#include "Utility.hpp"

#include <cstdlib>

namespace Psi {
#if PSI_DEBUG
  CheckedCastBase::~CheckedCastBase() {
  }
#endif
  
  WriteMemoryPool::WriteMemoryPool()
  : m_page_size(0x10000),
  m_pages(NULL) {
  }
  
  WriteMemoryPool::~WriteMemoryPool() {
    Page *p = m_pages;
    while (p) {
      Page *n = p->next;
      std::free(p);
      p = n;
    }
  }
  
  void* WriteMemoryPool::alloc(std::size_t size, std::size_t align) {
    if (m_pages) {
      std::size_t aligned_offset = (m_pages->offset + align - 1) & ~(align - 1);
      std::size_t end = size + aligned_offset;
      if (end <= m_pages->length) {
        char *ptr = m_pages->data + aligned_offset;
        m_pages->offset = end;
        return ptr;
      }
    }
    
    std::size_t new_page_size = std::max(size, m_page_size);
    Page *pg = static_cast<Page*>(std::malloc(sizeof(Page) + new_page_size));
    if (!pg)
      throw std::bad_alloc();
    pg->next = m_pages;
    pg->offset = size;
    pg->length = new_page_size;
    m_pages = pg;
    return pg->data;
  }
  
  char* WriteMemoryPool::str_alloc(std::size_t n) {
    return static_cast<char*>(alloc(n, 1));
  }
  
  char* WriteMemoryPool::strdup(const char *s) {
    std::size_t n = std::strlen(s) + 1;
    char *sc = str_alloc(n);
    std::memcpy(sc, s, n);
    return sc;
  }
}
