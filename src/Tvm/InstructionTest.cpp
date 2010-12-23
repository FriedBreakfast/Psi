#include "Test.hpp"

#include "Instructions.hpp"
#include "Jit.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(InstructionTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(ReturnIntConst) {
      const char *src =
        "%f = function cc_c () > int {\n"
        "  return #i19;\n"
        "};\n";

      Jit::Int32 (*f)() = reinterpret_cast<Jit::Int32(*)()>(jit_single("f", src));
      BOOST_CHECK_EQUAL(f(), 19);
    }

    BOOST_AUTO_TEST_CASE(ReturnIntParameter) {
      const char *src =
        "%f = function (%x:int) > int {\n"
        "  return %x;\n"
        "};\n";
      
      typedef void (*CallbackType) (void*,const void*);
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("f", src));

      const Jit::Int32 c = 143096367;
      Jit::Int32 result;
      f(&result, &c);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ReturnDependent) {
      const char *src =
        "%f = function(%a:type, %b:%a) > %a {\n"
        "  return %b;\n"
        "};\n";

      typedef void (*callback_type) (void*,const void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(jit_single("f", src));

      // a decent data size is required - previously a test of less
      // than 16 bytes worked previously for no known reason even
      // though the code generation wasn't working properly.
      const char data[] = "f4oh3g10845XweNNyu19hgb19";
      const Jit::Metatype data_meta = {sizeof(data), 1};

      char result_data[sizeof(data)];
      // Set to an incorrect value
      std::fill_n(result_data, sizeof(result_data), 'x');
      callback(result_data, &data_meta, data);
      BOOST_CHECK_EQUAL_COLLECTIONS(result_data, result_data+sizeof(result_data),
				    data, data+sizeof(data));
    }

    BOOST_AUTO_TEST_CASE(UnconditionalBranchTest) {
      const char *src =
        "%f = function cc_c () > int {\n"
        "  br %label;\n"
        "block %label:\n"
        "  return #i42389789;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) ();
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("f", src));

      BOOST_CHECK_EQUAL(f(), 42389789);
    }

    BOOST_AUTO_TEST_CASE(ConditionalBranchTest) {
      const char *src =
        "%f = function cc_c (%a:bool) > int {\n"
        "  cond_br %a %iftrue %iffalse;\n"
        "block %iftrue:\n"
        "  return #i344;\n"
        "block %iffalse:\n"
        "  return #i-102;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) (Jit::Boolean);
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("f", src));

      BOOST_CHECK_EQUAL(f(true), 344);
      BOOST_CHECK_EQUAL(f(false), -102);
    }

    BOOST_AUTO_TEST_CASE(RecursiveCall) {
      const char *src =
        "%inner = function () > int {\n"
        "  return #i40859;\n"
        "};\n"
        "\n"
        "%outer = function cc_c() > int {\n"
        "  %x = call inner;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) ();
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("outer", src));

      BOOST_CHECK_EQUAL(f(), 40859);
    }

    BOOST_AUTO_TEST_CASE(RecursiveCallParameter) {
      const char *src =
        "%inner = function (%a: int) > int {\n"
        "  return %a;\n"
        "};\n"
        "\n"
        "%outer = function cc_c(%a: int) > int {\n"
        "  %x = call inner %a;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) (Jit::Int32);
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("outer", src));

      BOOST_CHECK_EQUAL(f(439), 439);
      BOOST_CHECK_EQUAL(f(-34), -34);
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

      Jit::Int32 (*f)() = reinterpret_cast<Jit::Int32(*)()>(jit_single("main", src));
      Jit::Int32 result = f();
      BOOST_CHECK_EQUAL(result, 27);
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
      FuncType f = reinterpret_cast<FuncType>(jit_single("fn", src));
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
      FuncType f = reinterpret_cast<FuncType>(jit_single("test", src));
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
      FuncType f = reinterpret_cast<FuncType>(jit_single("f", src));
      BOOST_CHECK_EQUAL(f(true, 1, 2), 4);
      BOOST_CHECK_EQUAL(f(false, 5, 7), 14);
    }

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
