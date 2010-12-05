#include "Test.hpp"

#include "Function.hpp"
#include "Memory.hpp"
#include "JitTypes.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(MemoryTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(LoadTest) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%f = function cc_c (%p : (pointer %i32)) > %i32 {\n"
        "  %x = load %p;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*func_type) (Jit::Int32*);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      Jit::Int32 value = 2359;
      BOOST_CHECK_EQUAL(f(&value), 2359);
    }

    BOOST_AUTO_TEST_CASE(StoreTest) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%f = function cc_c (%x : %i32, %p : (pointer %i32)) > bool {\n"
        "  store %x %p;\n"
        "  return true;\n"
        "};\n";

      typedef Jit::Boolean (*func_type) (Jit::Int32,Jit::Int32*);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      Jit::Int32 value = 0;
      f(6817, &value);
      BOOST_CHECK_EQUAL(value, 6817);
    }

    namespace {
      Jit::Int32 alloca_test_cb(Jit::Int32 *ptr) {
        *ptr = 576;
        return 0;
      }
    }

    BOOST_AUTO_TEST_CASE(AllocaTest) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%f = function cc_c (%cb : (pointer (function cc_c ((pointer %i32))>%i32))) > %i32 {\n"
        "  %s = alloca %i32;\n"
        "  call %cb %s;\n"
        "  %x = load %s;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*func_type) (Jit::Int32(*)(Jit::Int32*));
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      BOOST_CHECK_EQUAL(f(alloca_test_cb), 576);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
