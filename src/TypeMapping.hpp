#ifndef HPP_PSI_TYPE_MAPPING
#define HPP_PSI_TYPE_MAPPING

/**
 * \file
 * 
 * Mapping of types to TVM.
 */

namespace Psi {
  struct MoveConstructible {
    void (*construct) (void *self, void *ptr);
    void (*destroy) (void *self, void *ptr);
    void (*move) (void *self, void *target, void *src);
    
    enum Members {
      m_construct=0,
      m_destroy=1,
      m_move=2
    };
  };
  
  struct CopyConstructible {
    MoveConstructible super;
    void (*copy) (void *self, void *target, void *src);
    void (*assign) (void *self, void *target, void *src);
    
    enum Members {
      m_super=0,
      m_copy=1,
      m_assign=2
    };
  };
}

#endif
