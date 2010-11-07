#ifndef HPP_PSI_TVM_JITTYPES
#define HPP_PSI_TVM_JITTYPES

#include <tr1/cstdint>

/**
 * \file Mapping to C types as used by the JIT compiler.
 */

namespace Psi {
  namespace Tvm {
    namespace Jit {
      typedef std::tr1::int8_t Boolean;

      typedef std::tr1::int8_t Int8;
      typedef std::tr1::int16_t Int16;
      typedef std::tr1::int32_t Int32;
      typedef std::tr1::int64_t Int64;

      typedef std::tr1::uint8_t UInt8;
      typedef std::tr1::uint16_t UInt16;
      typedef std::tr1::uint32_t UInt32;
      typedef std::tr1::uint64_t UInt64;

      typedef float Float;
      typedef double Double;

      /**
       * \brief Value type of MetatypeTerm.
       *
       * This is here for easy interfacing with C++ and must be kept in
       * sync with LLVMConstantBuilder::metatype_type.
       */
      struct Metatype {
	UInt64 size;
	UInt64 align;
      };
    }
  }
}

#endif
