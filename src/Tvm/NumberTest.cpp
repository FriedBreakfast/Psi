#include "Test.hpp"

#include "Number.hpp"
#include "Jit.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(NumberTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(IntType16) {
      const char *src = "%t = global const type i16;\n";
      const Jit::Metatype *mt = static_cast<Jit::Metatype*>(jit_single("t", src));
      BOOST_CHECK_EQUAL(mt->size, sizeof(Jit::Int16));
      BOOST_CHECK_EQUAL(mt->align, boost::alignment_of<Jit::Int16>::value);
    }

    BOOST_AUTO_TEST_CASE(IntValue) {
      const char *src = "%v = global const i32 #i4328950;\n";
      const Jit::Int32 *v = static_cast<Jit::Int32*>(jit_single("v", src));
      BOOST_CHECK_EQUAL(*v, 4328950);
    }

    BOOST_AUTO_TEST_CASE(IntegerAdd) {
      const char *src =
        "%add = function (%a:i32,%b:i32) > i32 {\n"
        "  return (add %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("add", src));
      BOOST_CHECK_EQUAL(f(344,-62), 282);
      BOOST_CHECK_EQUAL(f(-100,-1), -101);
    }

    BOOST_AUTO_TEST_CASE(IntegerSubtract) {
      const char *src =
        "%sub = function (%a:i32,%b:i32) > i32 {\n"
        "  return (sub %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("sub", src));
      BOOST_CHECK_EQUAL(f(12,-99), 111);
      BOOST_CHECK_EQUAL(f(34,27), 7);
    }

    BOOST_AUTO_TEST_CASE(IntegerMultiply) {
      const char *src =
        "%mul = function (%a:i32,%b:i32) > i32 {\n"
        "  return (mul %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("mul", src));
      BOOST_CHECK_EQUAL(f(4,5), 20);
      BOOST_CHECK_EQUAL(f(34,19), 646);
    }

    BOOST_AUTO_TEST_CASE(UnsignedIntegerDivide) {
      const char *src =
        "%udiv = function (%a:ui32,%b:ui32) > ui32 {\n"
        "  return (div %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("udiv", src));
      BOOST_CHECK_EQUAL(f(100, 20), 5);
      BOOST_CHECK_EQUAL(f(69, 7), 9);
    }

    BOOST_AUTO_TEST_CASE(SignedIntegerDivide) {
      const char *src =
        "%sdiv = function (%a:i32,%b:i32) > i32 {\n"
        "  return (div %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("sdiv", src));
      BOOST_CHECK_EQUAL(f(-15,-5), 3);
      BOOST_CHECK_EQUAL(f(-17,-5), 3); // check round-toward-zero behaviour
      BOOST_CHECK_EQUAL(f(35,-9), -3);
    }
    
    BOOST_AUTO_TEST_CASE(ShiftLeft) {
      const char *src =
        "%shift = function (%a:i32, %b: ui32, %c: ui32) > (struct i32 ui32) {\n"
        "  return (struct_v (shl %a %c) (shl %b %c));\n"
        "};\n";
        
      struct ResultType {Jit::Int32 a; Jit::UInt32 b;};
      typedef ResultType (*FuncType) (Jit::Int32, Jit::UInt32, Jit::UInt32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("shift", src));
      
      ResultType r1 = f(10, 17, 2);
      BOOST_CHECK_EQUAL(r1.a, 40);
      BOOST_CHECK_EQUAL(r1.b, 68);
    }
    
    BOOST_AUTO_TEST_CASE(ShiftRight) {
      const char *src =
        "%shift = function (%a:i32, %b: ui32, %c: ui32) > (struct i32 ui32) {\n"
        "  return (struct_v (shr %a %c) (shr %b %c));\n"
        "};\n";
        
      struct ResultType {Jit::Int32 a; Jit::UInt32 b;};
      typedef ResultType (*FuncType) (Jit::Int32, Jit::UInt32, Jit::UInt32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("shift", src));
      
      ResultType r2 = f(15, 29, 2);
      BOOST_CHECK_EQUAL(r2.a, 3);
      BOOST_CHECK_EQUAL(r2.b, 7);
      ResultType r3 = f(-5, -10, 2);
      BOOST_CHECK_EQUAL(r3.a, -2);
      BOOST_CHECK_EQUAL(r3.b, 0x3FFFFFFD);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
