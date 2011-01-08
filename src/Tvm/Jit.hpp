#ifndef HPP_PSI_TVM_JIT
#define HPP_PSI_TVM_JIT

#include "Core.hpp"

#include <boost/shared_ptr.hpp>

/**
 * \file
 *
 * Mapping to C types as used by the JIT compiler.
 */

namespace Psi {
  namespace Tvm {
    /**
     * \brief Base class for JIT compilers.
     *
     * Includes typedefs, which should conform to what the JIT maps a
     * given type to.
     */
    class Jit {
    public:
      typedef int8_t Boolean;

      typedef int8_t Int8;
      typedef int16_t Int16;
      typedef int32_t Int32;
      typedef int64_t Int64;
      typedef intptr_t IntPtr;

      typedef uint8_t UInt8;
      typedef uint16_t UInt16;
      typedef uint32_t UInt32;
      typedef uint64_t UInt64;
      typedef uintptr_t UIntPtr;

      typedef float Float;
      typedef double Double;

      /**
       * \brief Value type of Metatype.
       */
      struct Metatype {
	UIntPtr size;
	UIntPtr align;
      };

      virtual ~Jit() {}

      /**
       * \brief Get a pointer to the given term, generating code or
       * global data as necessary.
       */
      virtual void* get_global(GlobalTerm *global) = 0;
    };
  }
}

#endif
