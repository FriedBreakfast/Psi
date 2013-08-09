#include "Test.hpp"

#include "Number.hpp"
#include "Jit.hpp"

namespace Psi {
  namespace Tvm {
    PSI_TEST_SUITE_FIXTURE(NumberTest, Test::ContextFixture)

    PSI_TEST_CASE(IntType16) {
      const char *src = "%t = global const export type i16;\n";
      const Jit::Metatype *mt = static_cast<Jit::Metatype*>(jit_single("t", src));
      PSI_TEST_CHECK_EQUAL(mt->size, sizeof(Jit::Int16));
      PSI_TEST_CHECK_EQUAL(mt->align, boost::alignment_of<Jit::Int16>::value);
    }

    PSI_TEST_CASE(IntValue) {
      const char *src = "%v = global const export i32 #i4328950;\n";
      const Jit::Int32 *v = static_cast<Jit::Int32*>(jit_single("v", src));
      PSI_TEST_CHECK_EQUAL(*v, 4328950);
    }

    PSI_TEST_CASE(IntegerAdd) {
      const char *src =
        "%add = export function (%a:i32,%b:i32) > i32 {\n"
        "  return (add %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("add", src));
      PSI_TEST_CHECK_EQUAL(f(344,-62), 282);
      PSI_TEST_CHECK_EQUAL(f(-100,-1), -101);
    }

    PSI_TEST_CASE(IntegerSubtract) {
      const char *src =
        "%sub = export function (%a:i32,%b:i32) > i32 {\n"
        "  return (sub %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("sub", src));
      PSI_TEST_CHECK_EQUAL(f(12,-99), 111);
      PSI_TEST_CHECK_EQUAL(f(34,27), 7);
    }

    PSI_TEST_CASE(IntegerMultiply) {
      const char *src =
        "%mul = export function (%a:i32,%b:i32) > i32 {\n"
        "  return (mul %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("mul", src));
      PSI_TEST_CHECK_EQUAL(f(4,5), 20);
      PSI_TEST_CHECK_EQUAL(f(34,19), 646);
    }

    PSI_TEST_CASE(UnsignedIntegerDivide) {
      const char *src =
        "%udiv = export function (%a:ui32,%b:ui32) > ui32 {\n"
        "  return (div %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("udiv", src));
      PSI_TEST_CHECK_EQUAL(f(100, 20), 5);
      PSI_TEST_CHECK_EQUAL(f(69, 7), 9);
    }

    PSI_TEST_CASE(SignedIntegerDivide) {
      const char *src =
        "%sdiv = export function (%a:i32,%b:i32) > i32 {\n"
        "  return (div %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("sdiv", src));
      PSI_TEST_CHECK_EQUAL(f(-15,-5), 3);
      PSI_TEST_CHECK_EQUAL(f(-17,-5), 3); // check round-toward-zero behaviour
      PSI_TEST_CHECK_EQUAL(f(35,-9), -3);
    }
    
    PSI_TEST_CASE(ShiftLeft) {
      const char *src =
        "%shift = export function (%a:i32, %b: ui32, %c: ui32) > (struct i32 ui32) {\n"
        "  return (struct_v (shl %a %c) (shl %b %c));\n"
        "};\n";
        
      struct ResultType {Jit::Int32 a; Jit::UInt32 b;};
      typedef ResultType (*FuncType) (Jit::Int32, Jit::UInt32, Jit::UInt32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("shift", src));
      
      ResultType r1 = f(10, 17, 2);
      PSI_TEST_CHECK_EQUAL(r1.a, 40);
      PSI_TEST_CHECK_EQUAL(r1.b, 68u);
    }
    
    PSI_TEST_CASE(ShiftRight) {
      const char *src =
        "%shift = export function (%a:i32, %b: ui32, %c: ui32) > (struct i32 ui32) {\n"
        "  return (struct_v (shr %a %c) (shr %b %c));\n"
        "};\n";
        
      struct ResultType {Jit::Int32 a; Jit::UInt32 b;};
      typedef ResultType (*FuncType) (Jit::Int32, Jit::UInt32, Jit::UInt32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("shift", src));
      
      ResultType r2 = f(15, 29, 2);
      PSI_TEST_CHECK_EQUAL(r2.a, 3);
      PSI_TEST_CHECK_EQUAL(r2.b, 7u);
      ResultType r3 = f(-5, -10, 2);
      PSI_TEST_CHECK_EQUAL(r3.a, -2);
      PSI_TEST_CHECK_EQUAL(r3.b, 0x3FFFFFFDu);
    }

    PSI_TEST_SUITE_END()
  }
}
