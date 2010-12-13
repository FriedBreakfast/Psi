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

      typedef void (*FunctionType) (void*);
      const Jit::Int32 expected[] = {576, 34, 9};
      Jit::Int32 result[3];

      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));
      f(result);
      BOOST_CHECK_EQUAL_COLLECTIONS(expected, expected+3, result, result+3);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnByteArray) {
      const char *src =
        "%i8 = define (int #8);\n"
        "%f = function cc_c (%a:%i8,%b:%i8,%c:%i8,%d:%i8,%e:%i8,%f:%i8,%g:%i8,%h:%i8) > (array %i8 (c_uint #64 #8)) {\n"
        "  return (c_array %i8 %a %b %c %d %e %f %g %h);\n"
        "};\n";

      const Jit::Int8 x[] = {23, 34, 9, -19, 53, 95, -103, 2};
      struct ResultType {Jit::Int8 r[8];};
      typedef ResultType (*FunctionType) (Jit::Int8,Jit::Int8,Jit::Int8,Jit::Int8,
                                          Jit::Int8,Jit::Int8,Jit::Int8,Jit::Int8);

      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));
      ResultType r = f(x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]);
      BOOST_CHECK_EQUAL_COLLECTIONS(x,x+8,r.r,r.r+8);
    }

    struct TestStructType {
      Jit::Int32 a;
      Jit::Int64 b;
      Jit::Int16 c;
      Jit::Int32 d;
      Jit::Int8 e;

      bool operator == (const TestStructType& o) const {
        return (a==o.a) && (b==o.b) && (c==o.c) && (d==o.d) && (e==o.e);
      }

      friend std::ostream& operator << (std::ostream& os, const TestStructType& x) {
        return os << '{' << x.a << ',' << x.b << ',' << x.c << ',' << x.d << ',' << x.e << '}';
      }
    };

    BOOST_AUTO_TEST_CASE(GlobalConstStruct) {
      const char *src =
        "%ar = global const (struct (int #32) (int #64) (int #16) (int #32) (int #8))\n"
        "(c_struct\n"
        " (c_int #32 #134)\n"
        " (c_int #64 #654)\n"
        " (c_int #16 #129)\n"
        " (c_int #32 #43)\n"
        " (c_int #8 #7));\n";

      TestStructType expected = {134, 654, 129, 43, 7};
      const TestStructType *ptr = static_cast<TestStructType*>(jit_single("ar", src));
      BOOST_CHECK_EQUAL(expected, *ptr);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnStruct) {
      const char *src =
        "%at = define (struct (int #32) (int #64) (int #16) (int #32) (int #8));\n"
        "%f = function cc_c () > %at {\n"
        "  return (c_struct\n"
        "   (c_int #32 #541)\n"
        "   (c_int #64 #3590)\n"
        "   (c_int #16 #1)\n"
        "   (c_int #32 #155)\n"
        "   (c_int #8 #99));\n"
        "};\n";

      typedef TestStructType (*FunctionType) ();
      const TestStructType expected = {541, 3590, 1, 155, 99};

      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));
      TestStructType result = f();
      BOOST_CHECK_EQUAL(expected, result);
    }

    union TestUnionType {
      Jit::Int64 a;
      Jit::Int32 b[2];
    };

    BOOST_AUTO_TEST_CASE(GlobalConstUnion) {
      const char *src =
        "%u = define (union (int #64) (array (int #32) (c_uint #64 #2)));\n"
        "%ar = global const (array %u (c_uint #64 #2))\n"
        " (c_array %u\n"
        "  (c_union %u (c_int #64 #43256))\n"
        "  (c_union %u (c_array (int #32) (c_int #32 #14361) (c_int #32 #15))));\n";

      const TestUnionType *ptr = static_cast<TestUnionType*>(jit_single("ar", src));
      BOOST_CHECK_EQUAL(ptr[0].a, 43256);
      BOOST_CHECK_EQUAL(ptr[1].b[0], 14361);
      BOOST_CHECK_EQUAL(ptr[1].b[1], 15);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnUnion) {
      const char *src =
        "%u = define (union (int #64) (array (int #32) (c_uint #64 #2)));\n"
        "%f = function cc_c (%a:(int #64), %b:(int #32)) > (array %u (c_uint #64 #2)) {\n"
        "  return (c_array %u (c_union %u %a) (c_union %u (c_array (int #32) %b %b)));\n"
        "};\n";

      struct TestReturnType {TestUnionType u[2];};
      typedef TestReturnType (*FunctionType) (Jit::Int64,Jit::Int32);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      Jit::Int64 a = 5468768922;
      Jit::Int32 b = 4989;
      TestReturnType r = f(a, b);
      BOOST_CHECK_EQUAL(r.u[0].a, a);
      BOOST_CHECK_EQUAL(r.u[1].b[0], b);
      BOOST_CHECK_EQUAL(r.u[1].b[1], b);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
