#include "Test.hpp"
#include "Assembler.hpp"
#include "Jit.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(DerivedTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(GlobalConstArray) {
      const char *src =
        "%ar = global const (array i32 #up5)\n"
        " (array_v i32 #i1 #i5 #i17 #i9 #i2);\n";

      const Jit::Int32 expected[] = {1, 5, 17, 9, 2};
      const Jit::Int32 *ptr = static_cast<Jit::Int32*>(jit_single("ar", src));
      BOOST_CHECK_EQUAL_COLLECTIONS(expected, expected+5, ptr, ptr+5);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnByteArray) {
      const char *src =
        "%f = function (%a:i8,%b:i8,%c:i8,%d:i8,%e:i8,%f:i8,%g:i8,%h:i8) > (array i8 #up8) {\n"
        "  return (array_v i8 %a %b %c %d %e %f %g %h);\n"
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
        "%ar = global const (struct i32 i64 i16 i32 i8)\n"
        "(struct_v #i134 #l654 #s129 #i43 #b7);\n";

      TestStructType expected = {134, 654, 129, 43, 7};
      const TestStructType *ptr = static_cast<TestStructType*>(jit_single("ar", src));
      BOOST_CHECK_EQUAL(expected, *ptr);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnStruct) {
      const char *src =
        "%at = define (struct i32 i64 i16 i32 i8);\n"
        "%f = function () > %at {\n"
        "  return (struct_v #i541 #l3590 #s1 #i155 #b99);\n"
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
        "%u = define (union i64 (array i32 #up2));\n"
        "%ar = global const (array %u #up2)\n"
        " (array_v %u (union_v %u #l43256) (union_v %u (array_v i32 #i14361 #i15)));\n";

      const TestUnionType *ptr = static_cast<TestUnionType*>(jit_single("ar", src));
      BOOST_CHECK_EQUAL(ptr[0].a, 43256);
      BOOST_CHECK_EQUAL(ptr[1].b[0], 14361);
      BOOST_CHECK_EQUAL(ptr[1].b[1], 15);
    }

    BOOST_AUTO_TEST_CASE(FunctionReturnUnion) {
      const char *src =
        "%u = define (union i64 (array i32 #up2));\n"
        "%f = function (%a:i64, %b:i32) > (array %u #up2) {\n"
        "  return (array_v %u (union_v %u %a) (union_v %u (array_v i32 %b %b)));\n"
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

    BOOST_AUTO_TEST_CASE(FunctionParameterUnion) {
      const char *src =
        "%u = define (union i64 (array i32 #up2));\n"
        "%f = function (%a:%u) > %u {\n"
        "  return %a;\n"
        "};\n";

      typedef TestUnionType (*FunctionType) (TestUnionType);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      TestUnionType u1, u2;
      u1.a = 904589786;
      u2.b[0] = 4956;
      u2.b[1] = 120954;

      TestUnionType r1 = f(u1), r2 = f(u2);
      BOOST_CHECK_EQUAL(r1.a, u1.a);
      BOOST_CHECK_EQUAL(r2.b[0], u2.b[0]);
      BOOST_CHECK_EQUAL(r2.b[1], u2.b[1]);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
