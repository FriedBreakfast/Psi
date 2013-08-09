#include <boost/test/unit_test.hpp>

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Number.hpp"
#include "Instructions.hpp"
#include "Jit.hpp"

#include "Test.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(FunctionTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(PhantomParameterTest) {
      const char *src =
        "%f = export function (%a:type|%b:bool,%c:(pointer %a),%d:(pointer %a)) > (pointer %a) {\n"
        " cond_br %b %tc %td;\n"
        "block %tc:\n"
        " return %c;\n"
        "block %td:\n"
        " return %d;\n"
        "};\n";

      typedef void* (*FunctionType) (Jit::Boolean,void*,void*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      int x, y;
      BOOST_CHECK_EQUAL(f(true, &x, &y), &x);
      BOOST_CHECK_EQUAL(f(false, &x, &y), &y);
    }

    namespace {
      void* return_1(void*x,void*) {return x;}
      void* return_2(void*,void*x) {return x;}
    }

    BOOST_AUTO_TEST_CASE(PhantomCallbackTest) {
      const char *src =
        "%f = export function (%a:type|%b:(pointer(function cc_c ((pointer %a),(pointer %a))>(pointer %a))),%c:(pointer %a),%d:(pointer %a)) > (pointer %a) {\n"
        "  %r = call %b %c %d;\n"
        "  return %r;\n"
        "};\n";

      typedef void* (*FunctionType) (void*(*)(void*,void*),void*,void*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      int x, y;
      BOOST_CHECK_EQUAL(f(return_1,&x,&y), &x);
      BOOST_CHECK_EQUAL(f(return_2,&x,&y), &y);
    }

    BOOST_AUTO_TEST_CASE(PhiTest) {
      const char *src =
        "%f = export function (%a: bool, %b: i32, %c: i32) > i32 {\n"
        "  cond_br %a %tb %tc;\n"
        "block %tb:\n"
        "  br %end;\n"
        "block %tc:\n"
        "  br %end;\n"
        "block %end:\n"
        "  %r = phi i32: %tb > %b, %tc > %c;\n"
        "  return %r;\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Boolean,Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("f", src));
      BOOST_CHECK_EQUAL(f(true, 10, 25), 10);
      BOOST_CHECK_EQUAL(f(false, 15, 30), 30);
    }
    
    BOOST_AUTO_TEST_CASE(PhiEdgeTest) {
      const char *src =
        "%f = export function (%a: bool, %b: i32, %c: i32) > i32 {\n"
        "  br %entry;\n"
        "block %entry:\n"
        "  %x = alloca i32;\n"
        "  store %b %x;\n"
        "  cond_br %a %tb %tc;\n"
        "block %tb(%entry):\n"
        "  %y = load %x;\n"
        "  return %y;\n"
        "block %tc:\n"
        "  return %c;\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Boolean,Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("f", src));
      BOOST_CHECK_EQUAL(f(true, 10, 25), 10);
      BOOST_CHECK_EQUAL(f(false, 15, 30), 30);
    }

    BOOST_AUTO_TEST_SUITE_END()
 }
}
