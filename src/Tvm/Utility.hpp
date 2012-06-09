#ifndef HPP_PSI_TVM_UTILITY
#define HPP_PSI_TVM_UTILITY

#include <algorithm>

#include <boost/type_traits/alignment_of.hpp>

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Compute the offset to the next field.
     *
     * \param base Offset to the current field
     * \param size Size of the current field
     * \param align Alignment of the next field
     */
    inline std::size_t struct_offset(std::size_t base, std::size_t size, std::size_t align) {
      return (base + size + align - 1) & ~(align - 1);
    }

    /**
     * \brief Offset a pointer by a specified number of bytes.
     */
    inline void* ptr_offset(void *p, std::size_t offset) {
      return static_cast<void*>(static_cast<char*>(p) + offset);
    }
  }
}

#endif
