#include "Test.hpp"
#include "Assembler.hpp"
#include "JitTypes.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Tvm {
    namespace {
      struct F : Test::ContextFixture {
        void* jit(const char *name, const char *src) {
          AssemblerResult r = parse_and_build(context, src);
          AssemblerResult::iterator it = r.find(name);
          BOOST_REQUIRE(it != r.end());
          void *result = context.term_jit(it->second);
          return result;
        };
      };
    }

    BOOST_FIXTURE_TEST_SUITE(TvmAssemblerTest, F)

    BOOST_AUTO_TEST_CASE(Return) {
      const char *src =
        "%main = function cc_c () > (int #32) {\n"
        "  return (c_int #32 #19);"
        "};\n";

      Jit::Int32 (*f)() = reinterpret_cast<Jit::Int32(*)()>(jit("main", src));
      Jit::Int32 result = f();
      BOOST_CHECK_EQUAL(result, 19);
    }

    BOOST_AUTO_TEST_CASE(Recursion) {
      const char *src =
        "%i32 = define (int #32);\n"
        "\n"
        "%x = function (%a:%i32,%b:%i32) > %i32 {\n"
        "  return (add %a %b);"
        "};\n"
        "\n"
        "%main = function cc_c () > %i32 {\n"
        "  %n = call %x (c_int #32 #19) (c_int #32 #8);\n"
        "  return %n;\n"
        "};\n";

      Jit::Int32 (*f)() = reinterpret_cast<Jit::Int32(*)()>(jit("main", src));
      Jit::Int32 result = f();
      BOOST_CHECK_EQUAL(result, 27);
    }

    BOOST_AUTO_TEST_CASE(Multiply) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%mul = function cc_c (%a:%i32,%b:%i32) > %i32 {\n"
        "  return (mul %a %b);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit("mul", src));
      BOOST_CHECK_EQUAL(f(4,5), 20);
      BOOST_CHECK_EQUAL(f(34,19), 646);
    }

    BOOST_AUTO_TEST_CASE(ConditionalBranch) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%fn = function cc_c (%a:bool,%b:%i32,%c:%i32) > %i32 {\n"
        "  cond_br %a %if_true %if_false;\n"
        "  %sum = add %b %c;\n"
        "  %dif = sub %b %c;\n"
        "block %if_true:\n"
        "  return %sum;\n"
        "block %if_false:\n"
        "  return %dif;"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Boolean,Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit("fn", src));
      BOOST_CHECK_EQUAL(f(true, 10, 25), 35);
      BOOST_CHECK_EQUAL(f(false, 10, 25), -15);
      BOOST_CHECK_EQUAL(f(true, 15, 30), 45);
      BOOST_CHECK_EQUAL(f(false, 15, 30), -15);
    }

    BOOST_AUTO_TEST_CASE(FunctionPointer) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%i16 = define (int #16);\n"
        "\n"
        "%add16 = function (%a:%i16,%b:%i16) > %i16 {\n"
        "  return (add %a %b);\n"
        "};\n"
        "\n"
        "%add32 = function (%a:%i32,%b:%i32) > %i32 {\n"
        "  return (add %a %b);\n"
        "};\n"
        "\n"
        "%bincb = function (%t:type,%a:%t,%b:%t,%f:(pointer (function (%t,%t) > %t))) > %t {\n"
        "  %r = call %f %a %b;\n"
        "  return %r;\n"
        "};\n"
        "\n"
        "%test = function cc_c () > bool {\n"
        "  %rx = call %bincb %i32 (c_int #32 #25) (c_int #32 #17) %add32;\n"
        "  %ry = call %bincb %i16 (c_int #16 #44) (c_int #16 #5) %add16;\n"
        "  return true;\n"
        "};\n";

      typedef Jit::Boolean (*FuncType) ();
      FuncType f = reinterpret_cast<FuncType>(jit("test", src));
      BOOST_CHECK_EQUAL(f(), true);
    }

    /*
     * Test that functional operations used in functions have their
     * code generated in the correct location, i.e. the dominating
     * block of their input values. If the code is generated
     * incorrectly, one branch will not be able to see the resulting
     * value hence LLVM should fail to compile.
     */
    BOOST_AUTO_TEST_CASE(FunctionalOperationDominatorGenerate) {
      const char *src =
        "%i32 = define (int #32);\n"
        "%f = function cc_c (%a: bool, %b: %i32, %c: %i32) > %i32 {\n"
        "  %t = add %b %c;\n"
        "  cond_br %a %tc %fc;\n"
        "block %tc:\n"
        "  return (add %t (c_int #32 #1));\n"
        "block %fc:\n"
        "  return (add %t (c_int #32 #2));\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Boolean,Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit("f", src));
      BOOST_CHECK_EQUAL(f(true, 1, 2), 4);
      BOOST_CHECK_EQUAL(f(false, 5, 7), 14);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
