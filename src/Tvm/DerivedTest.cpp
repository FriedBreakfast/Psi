#include "Test.hpp"
#include "Assembler.hpp"
#include "JitTypes.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(DerivedTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(GlobalConstArray) {
      const char *src =
        "%ar = global const (array (int #32) (c_uint #64 #5))\n"
        "(c_array (int #32)\n"
        " (c_int #32 #1)\n"
        " (c_int #32 #5)\n"
        " (c_int #32 #17)\n"
        " (c_int #32 #9)\n"
        " (c_int #32 #2));\n";

      const Jit::Int32 expected[] = {1, 5, 17, 9, 2};
      const Jit::Int32 *ptr = static_cast<Jit::Int32*>(jit_single("ar", src));
      BOOST_CHECK_EQUAL_COLLECTIONS(expected, expected+5, ptr, ptr+5);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnArray) {
      const char *src =
        "%at = define (array (int #32) (c_uint #64 #3));\n"
        "%f = function () > %at {\n"
        "  return (c_array (int #32) (c_int #32 #576) (c_int #32 #34) (c_int #32 #9));\n"
        "};\n";

      typedef void* (*FunctionType) (void*);
      const Jit::Int32 expected[] = {576, 34, 9};
      Jit::Int32 result[3];

      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));
      void *ptr = f(result);
      BOOST_CHECK_EQUAL(ptr, result);
      BOOST_CHECK_EQUAL_COLLECTIONS(expected, expected+3, result, result+3);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
