#include "Test.hpp"

#include "Instructions.hpp"
#include "Jit.hpp"

#if PSI_HAVE_UCONTEXT
#include <signal.h>
#include <ucontext.h>
#endif

namespace Psi {
  namespace Tvm {
    PSI_TEST_SUITE_FIXTURE(InstructionTest, Test::ContextFixture)

    PSI_TEST_CASE(ReturnIntConst) {
      const char *src =
        "%f = export function () > i32 {\n"
        "  return #i19;\n"
        "};\n";

      Jit::Int32 (*f)() = reinterpret_cast<Jit::Int32(*)()>(jit_single("f", src));
      PSI_TEST_CHECK_EQUAL(f(), 19);
    }

    PSI_TEST_CASE(ReturnIntParameter) {
      const char *src =
        "%f = export function (%x:i32) > i32 {\n"
        "  return %x;\n"
        "};\n";
      
      typedef Jit::Int32 (*CallbackType) (Jit::Int32);
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("f", src));

      const Jit::Int32 c = 143096367;
      PSI_TEST_CHECK_EQUAL(f(c), c);
    }

    PSI_TEST_CASE(UnconditionalBranchTest) {
      const char *src =
        "%f = export function () > i32 {\n"
        "  br %label;\n"
        "block %label:\n"
        "  return #i42389789;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) ();
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("f", src));

      PSI_TEST_CHECK_EQUAL(f(), 42389789);
    }

    PSI_TEST_CASE(ConditionalBranchTest) {
      const char *src =
        "%f = export function (%a:bool) > i32 {\n"
        "  cond_br %a %iftrue %iffalse;\n"
        "block %iftrue:\n"
        "  return #i344;\n"
        "block %iffalse:\n"
        "  return #i-102;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) (Jit::Boolean);
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("f", src));

      PSI_TEST_CHECK_EQUAL(f(true), 344);
      PSI_TEST_CHECK_EQUAL(f(false), -102);
    }

    PSI_TEST_CASE(RecursiveCall) {
      const char *src =
        "%inner = function () > i32 {\n"
        "  return #i40859;\n"
        "};\n"
        "\n"
        "%outer = export function () > i32 {\n"
        "  %x = call %inner;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) ();
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("outer", src));

      PSI_TEST_CHECK_EQUAL(f(), 40859);
    }

    PSI_TEST_CASE(RecursiveCallParameter) {
      const char *src =
        "%inner = function (%a: i32) > i32 {\n"
        "  return %a;\n"
        "};\n"
        "\n"
        "%outer = export function (%a: i32) > i32 {\n"
        "  %x = call %inner %a;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*CallbackType) (Jit::Int32);
      CallbackType f = reinterpret_cast<CallbackType>(jit_single("outer", src));

      PSI_TEST_CHECK_EQUAL(f(439), 439);
      PSI_TEST_CHECK_EQUAL(f(-34), -34);
    }

    PSI_TEST_CASE(Recursion) {
      const char *src =
        "%x = function (%a:i32,%b:i32) > i32 {\n"
        "  return (add %a %b);"
        "};\n"
        "\n"
        "%main = export function () > i32 {\n"
        "  %n = call %x #i19 #i8;\n"
        "  return %n;\n"
        "};\n";

      Jit::Int32 (*f)() = reinterpret_cast<Jit::Int32(*)()>(jit_single("main", src));
      Jit::Int32 result = f();
      PSI_TEST_CHECK_EQUAL(result, 27);
    }

    PSI_TEST_CASE(ConditionalBranch) {
      const char *src =
        "%fn = export function (%a:bool,%b:i32,%c:i32) > i32 {\n"
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
      PSI_TEST_CHECK_EQUAL(f(true, 10, 25), 35);
      PSI_TEST_CHECK_EQUAL(f(false, 10, 25), -15);
      PSI_TEST_CHECK_EQUAL(f(true, 15, 30), 45);
      PSI_TEST_CHECK_EQUAL(f(false, 15, 30), -15);
    }

    PSI_TEST_CASE(FunctionPointer) {
      const char *src =
        "%pi16 = define pointer i16;\n"
        "%pi32 = define pointer i32;\n"
        "\n"
        "%add16 = function (%a:%pi16,%b:%pi16,%c:%pi16) > empty {\n"
        "  %av = load %a;\n"
        "  %bv = load %b;\n"
        "  store (add %av %bv) %c;\n"
        "  return empty_v;\n"
        "};\n"
        "\n"
        "%add32 = function (%a:%pi32,%b:%pi32,%c:%pi32) > empty {\n"
        "  %av = load %a;\n"
        "  %bv = load %b;\n"
        "  store (add %av %bv) %c;\n"
        "  return empty_v;\n"
        "};\n"
        "\n"
        "%bincb = function (%t:type,%a:pointer %t,%b:pointer %t,%f:pointer (function (pointer %t,pointer %t,pointer %t) > empty),%o:pointer %t) > empty {\n"
        "  call %f %a %b %o;\n"
        "  return empty_v;\n"
        "};\n"
        "\n"
        "%test = export function (%m : %pi32, %n : %pi16) > bool {\n"
        "  %x = alloca i32 #up2 #up1;\n"
        "  store #i25 %x;\n"
        "  store #i17 (pointer_offset %x #p1);\n"
        "  call %bincb i32 %x (pointer_offset %x #p1) %add32 %m;\n"
        "  %y = alloca i16 #up2 #up1;\n"
        "  store #s44 %y;\n"
        "  store #s5 (pointer_offset %y #p1);\n"
        "  call %bincb i16 %y (pointer_offset %y #p1) %add16 %n;\n"
        "  return true;\n"
        "};\n";

      typedef Jit::Boolean (*FuncType) (Jit::Int32*,Jit::Int16*);
      Jit::Int32 i32;
      Jit::Int16 i16;
      FuncType f = reinterpret_cast<FuncType>(jit_single("test", src));
      PSI_TEST_CHECK_EQUAL(f(&i32, &i16), true);
      PSI_TEST_CHECK_EQUAL(i32, 42);
      PSI_TEST_CHECK_EQUAL(i16, 49);
    }

    /*
     * Test that functional operations used in functions have their
     * code generated in the correct location, i.e. the dominating
     * block of their input values. If the code is generated
     * incorrectly, one branch will not be able to see the resulting
     * value hence LLVM should fail to compile.
     */
    PSI_TEST_CASE(FunctionalOperationDominatorGenerate) {
      const char *src =
        "%f = export function (%a: bool, %b: i32, %c: i32) > i32 {\n"
        "  %t = add %b %c;\n"
        "  cond_br %a %tc %fc;\n"
        "block %tc:\n"
        "  return (add %t #i1);\n"
        "block %fc:\n"
        "  return (add %t #i2);\n"
        "};\n";

      typedef Jit::Int32 (*FuncType) (Jit::Boolean,Jit::Int32,Jit::Int32);
      FuncType f = reinterpret_cast<FuncType>(jit_single("f", src));
      PSI_TEST_CHECK_EQUAL(f(true, 1, 2), 4);
      PSI_TEST_CHECK_EQUAL(f(false, 5, 7), 14);
    }

    PSI_TEST_CASE(LoadTest) {
      const char *src =
        "%f = export function (%p : (pointer i32)) > i32 {\n"
        "  %x = load %p;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*func_type) (Jit::Int32*);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      Jit::Int32 value = 2359;
      PSI_TEST_CHECK_EQUAL(f(&value), 2359);
    }

    PSI_TEST_CASE(StoreTest) {
      const char *src =
        "%f = export function (%x : i32, %p : (pointer i32)) > bool {\n"
        "  store %x %p;\n"
        "  return true;\n"
        "};\n";

      typedef Jit::Boolean (*func_type) (Jit::Int32,Jit::Int32*);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      Jit::Int32 value = 0;
      f(6817, &value);
      PSI_TEST_CHECK_EQUAL(value, 6817);
    }
    
    PSI_TEST_CASE(LoadStoreOrderTest) {
      const char *src =
        "%f = export function (%x : i32, %y : (pointer i32)) > i32 {\n"
        "  %a = load %y;\n"
        "  store %x %y;\n"
        "  return %a;\n"
        "};\n";
        
      typedef Jit::Int32 (*func_type) (Jit::Int32,Jit::Int32*);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));
      const Jit::Int32 a = 32, b = 54;
      Jit::Int32 dat[1] = {b};
      Jit::Int32 r = f(a, dat);
      PSI_TEST_CHECK_EQUAL(dat[0], a);
      PSI_TEST_CHECK_EQUAL(r, b);
    }

    namespace {
      Jit::Int32 alloca_test_cb(Jit::Int32 *ptr) {
        *ptr = 576;
        return 0;
      }
    }

    PSI_TEST_CASE(AllocaTest) {
      const char *src =
        "%f = export function (%cb : (pointer (function cc_c ((pointer i32))>i32))) > i32 {\n"
        "  %s = alloca i32 #up1 #up1;\n"
        "  call %cb %s;\n"
        "  %x = load %s;\n"
        "  return %x;\n"
        "};\n";

      typedef Jit::Int32 (*func_type) (Jit::Int32(*)(Jit::Int32*));
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      PSI_TEST_CHECK_EQUAL(f(alloca_test_cb), 576);
    }
    
    PSI_TEST_CASE(SolidifyTest) {
      const char *src = 
        "%f = export function(%a : i32 | %x : (constant %a)) > i32 {\n"
        "  solidify %x;\n"
        "  return %a;\n"
        "};\n";

      typedef Jit::Int32 (*func_type) (Jit::Int32);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));

      Jit::Int32 v = 42350898;
      PSI_TEST_CHECK_EQUAL(f(v), v);
    }
    
    PSI_TEST_CASE(ConstantTypeZeroTest) {
      const char *src = 
        "%f = export function(%a : i32, %p : pointer (constant %a)) > (constant %a) {\n"
        "  %z = zero (constant %a);\n"
        "  store %z %p;\n"
        "  return %z;\n"
        "};\n";
        
      typedef Jit::Int32 (*func_type) (Jit::Int32, Jit::Int32*);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));
      
      Jit::Int32 v = -1985092;
      Jit::Int32 a, b;
      a = f(v, &b);
      PSI_TEST_CHECK_EQUAL(a, v);
      PSI_TEST_CHECK_EQUAL(b, v);
    }
    
    /*
     * Check that alloca() in a loop reuses memory from previous iterations.
     */
    PSI_TEST_CASE(StackAllocLoopTest) {
      const char *src =
        "%f = export function(%c : uiptr, %n : uiptr, %r : pointer (array (pointer i8) %n)) > empty {\n"
        "  br %entry;\n"
        "block %entry:\n"
        "  %idx = phi uiptr: > #up0, %body > (add %idx #up1);\n"
        "  %test = cmp_lt %idx %n;\n"
        "  cond_br %test %body %exit;\n"
        "block %body(%entry):\n"
        "  %p = alloca i8 %c;\n"
        "  store %p (gep %r %idx);\n"
        "  br %entry;\n"
        "block %exit:\n"
        "  return empty_v;\n"
        "};\n";
        
      typedef void (*func_type) (Jit::IntPtr,Jit::IntPtr,Jit::Int8**);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));
      
      const Jit::IntPtr loop_count = 100;
      Jit::Int8* pointers[loop_count];
      Jit::IntPtr alloc_size = 1000;
      
      f(alloc_size, loop_count, pointers);
      PSI_TEST_CHECK(std::abs(static_cast<std::ptrdiff_t>(pointers[loop_count - 1] - pointers[0])) < alloc_size);
    }
    
    PSI_TEST_CASE(EvaluateTest1) {
      const char *src =
        "%f = export function(%cond : bool, %denom : ui32) > ui32 {\n"
        "  %ex = (div #ui1 %denom);\n"
        "  cond_br %cond %b1 %b2;\n"
        "block %b1:\n"
        "  return #ui0;\n"
        "block %b2:\n"
        "  return %ex;\n"
        "};\n";

      typedef void (*func_type) (Jit::Boolean, Jit::UInt32);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));
      f(true, 0);
    }
    
#if PSI_HAVE_UCONTEXT
    namespace EvaluateTest2Help {
      bool tripped;
      ucontext_t context;

      void action(int, siginfo_t*, void*) {
        tripped = true;
        setcontext(&context);
        std::abort();
      }
      
      bool wrapper(void (*f) (Jit::Boolean, Jit::UInt32)) {
        struct sigaction new_act, old_act;
        new_act.sa_sigaction = action;
        sigemptyset(&new_act.sa_mask);
        new_act.sa_flags = SA_SIGINFO;
        sigaction(SIGFPE, &new_act, &old_act);

        tripped = false;
        getcontext(&context);
        if (tripped) {
          sigaction(SIGFPE, &old_act, NULL);
          return true;
        }

        f(true, 0);
        sigaction(SIGFPE, &old_act, NULL);
        
        return false;
        
      }
    }
    
    PSI_TEST_CASE(EvaluateTest2) {
      const char *src =
        "%f = export function(%cond : bool, %denom : ui32) > ui32 {\n"
        "  %ex = (div #ui1 %denom);\n"
        "  eval %ex;\n"
        "  cond_br %cond %b1 %b2;\n"
        "block %b1:\n"
        "  return #ui0;\n"
        "block %b2:\n"
        "  return %ex;\n"
        "};\n";

      typedef void (*func_type) (Jit::Boolean, Jit::UInt32);
      func_type f = reinterpret_cast<func_type>(jit_single("f", src));
      
      PSI_TEST_CHECK(EvaluateTest2Help::wrapper(f));
    }
#endif

    PSI_TEST_SUITE_END()    
  }
}
