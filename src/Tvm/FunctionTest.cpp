#include <boost/test/unit_test.hpp>

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Number.hpp"
#include "ControlFlow.hpp"
#include "Primitive.hpp"
#include "JitTypes.hpp"

#include "Test.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(FunctionTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(FunctionTypeTest) {
      IntegerType i32(true, 32);
      Term *i32_t = context.get_functional_v(i32);
      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i32_t);

      BOOST_CHECK_EQUAL(func_type->calling_convention(), cconv_tvm);
      BOOST_CHECK_EQUAL(func_type->result_type(), i32_t); 
    }

    BOOST_AUTO_TEST_CASE(CCall_ReturnInt) {
      const Jit::Int32 c = 45878594;

      IntegerType i32(true, 32);
      Term* i32_t = context.get_functional_v(i32);
      Term* value = context.get_functional_v(ConstantInteger(i32, c));

      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(cconv_c, i32_t);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), value);

      typedef Jit::Int32 (*callback_type) ();
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      Jit::Int32 result = callback();
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(CCall_ReturnIntParameter) {
      const Jit::Int32 c = 258900654;

      Term* i32_t = context.get_functional_v(IntegerType(true, 32));
      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(cconv_c, i32_t, i32_t);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), func->parameter(0));

      typedef Jit::Int32 (*callback_type) (Jit::Int32);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      Jit::Int32 result = callback(c);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(PhantomParameterTest) {
      const char *src =
        "%f = function cc_c (%a:type|%b:bool,%c:(pointer %a),%d:(pointer %a)) > (pointer %a) {\n"
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
        "%f = function cc_c(%a:type|%b:(pointer(function cc_c ((pointer %a),(pointer %a))>(pointer %a))),%c:(pointer %a),%d:(pointer %a)) > (pointer %a) {\n"
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
        "%i32 = define (int #32);\n"
        "%f = function cc_c (%a: bool, %b: %i32, %c: %i32) > %i32 {\n"
        "  cond_br %a %tb %tc;\n"
        "block %tb:\n"
        "  br %end;\n"
        "block %tc:\n"
        "  br %end;\n"
        "block %end:\n"
        "  %r = phi %i32: %tb > %b, %tc > %c;\n"
        "  return %r;\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Boolean,Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("f", src));
      BOOST_CHECK_EQUAL(f(true, 10, 25), 10);
      BOOST_CHECK_EQUAL(f(false, 15, 30), 30);
    }

    BOOST_AUTO_TEST_CASE(PhiUnknownTest) {
      const char *src =
        "%f = function (%a: bool, %b:type, %c: %b, %d: %b) > %b {\n"
        "  cond_br %a %tc %td;\n"
        "block %tc:\n"
        "  br %end;\n"
        "block %td:\n"
        "  br %end;\n"
        "block %end:\n"
        "  %r = phi %b: %tc > %c, %td > %d;\n"
        "  return %r;\n"
        "};\n";

      const char test1[] = "gqgh9-1h1hu-";
      const char test2[] = "1390g9010hh9";
      BOOST_REQUIRE_EQUAL(sizeof(test1), sizeof(test2));

      Jit::Metatype mt = {sizeof(test1), 1};
      typedef Jit::Int32 (*FuncType) (char*, const bool*, const Jit::Metatype*, const char*, const char*);
      FuncType f = reinterpret_cast<FuncType>(jit_single("f", src));

      bool cond;
      char result_area[sizeof(test1)];

      cond = true;
      f(result_area, &cond, &mt, test1, test2);
      BOOST_CHECK_EQUAL_COLLECTIONS(result_area, result_area+sizeof(result_area),
                                    test1, test1+sizeof(test1));

      cond = false;
      f(result_area, &cond, &mt, test1, test2);
      BOOST_CHECK_EQUAL_COLLECTIONS(result_area, result_area+sizeof(result_area),
                                    test2, test2+sizeof(test2));
    }

    BOOST_AUTO_TEST_SUITE_END()
 }
}
