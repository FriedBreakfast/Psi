#include "Test.hpp"

#include "Aggregate.hpp"
#include "Jit.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(AggregateTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(EmptyStructTest) {
      const char *src = "%es = global const export type struct;\n";

      Jit::Metatype *mt = static_cast<Jit::Metatype*>(jit_single("es", src));
      BOOST_CHECK_EQUAL(mt->size, 0);
      BOOST_CHECK_EQUAL(mt->align, 1);
    }
    
    BOOST_AUTO_TEST_CASE(DownUpRefTest) {
      const char *src =
        "%s = define struct i32 i32;\n"
        "%f = export function (%a:pointer %s) > (pointer %s) {\n"
        " return (outer_ptr (gep %a #up1));\n"
        "};\n";

      typedef void* (*FunctionType) (void*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      int x[2];
      BOOST_CHECK_EQUAL(f(x), x);
    }
    
    BOOST_AUTO_TEST_CASE(ApplyValueTest) {
      const char *src =
        "%tt = recursive () > (struct i32 i32);\n"
        "%ty = define apply %tt;\n"
        "%f = export function (%a:i32, %b:i32) > %ty {\n"
        "  return (apply_v %ty (struct_v %a %b));\n"
        "};\n";

      struct ResultType {Jit::Int32 a, b;};
      typedef ResultType (*FunctionType) (Jit::Int32, Jit::Int32);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      ResultType in = {56, -90159};
      ResultType out = f(in.a, in.b);
      BOOST_CHECK_EQUAL(in.a, out.a);
      BOOST_CHECK_EQUAL(in.b, out.b);
    }
    
    namespace {
      struct DispatchTestVtable {
        int32_t (*callback) (void *self);
      };
      
      struct DispatchTestObject {
        DispatchTestVtable *vptr;
        int32_t value;
      };
      
      int32_t dispatch_test_callback(void *self) {
        return ((DispatchTestObject*)self)->value;
      }
    }
    
    BOOST_AUTO_TEST_CASE(DispatchTest) {
      const char *src =
        "%vtable = recursive (%tag : upref_type) > (struct\n"
        "  (pointer (function (pointer (apply %base %tag) %tag) > i32))\n"
        ");\n"
        "\n"
        "%base = recursive (%tag : upref_type) > (struct\n"
        "  (pointer (apply %vtable %tag))\n"
        ");\n"
        "\n"
        "%func = export function (%obj_wrapped : exists (%tag : upref_type) > (pointer (apply %base %tag) %tag)) > i32 {\n"
        "  %obj = unwrap %obj_wrapped;\n"
        "  %vptr = load (gep (gep %obj #up0) #up0);\n"
        "  %callback = load (gep (gep %vptr #up0) #up0);\n"
        "  %val = call %callback %obj;\n"
        "  return %val;\n"
        "};\n";

      typedef int32_t (*FunctionType) (DispatchTestObject*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("func", src));
      
      DispatchTestVtable vtable = {dispatch_test_callback};
      DispatchTestObject obj = {&vtable, 0};
      BOOST_CHECK_EQUAL(f(&obj), 0);
      obj.value = 30;
      BOOST_CHECK_EQUAL(f(&obj), 30);
    }
    
    namespace {
      struct DerivedDispatchTestVtable {
        DispatchTestVtable base;
        int32_t (*callback) (void *self);
      };
      
      struct DerivedDispatchTestObject {
        DispatchTestObject base;
        int32_t value;
      };
      
      int32_t derived_dispatch_test_callback(void *self) {
        return ((DerivedDispatchTestObject*)self)->value;
      }
    }

    BOOST_AUTO_TEST_CASE(InheritanceDispatchTest) {
      const char *src =
        "%vtable = recursive (%vtag : upref_type, %tag : upref_type) > (struct\n"
        "  (pointer (function (pointer (apply %base %vtag %tag) %tag) > i32))\n"
        ");\n"
        "\n"
        "%vtable_derived = recursive (%vtag : upref_type, %tag : upref_type) > (struct\n"
        "  (apply %vtable (upref %vtag (apply %vtable_derived %vtag %tag) #up0 #up0) (upref %tag (apply %derived %vtag %tag) #up0 #up0))\n"
        "  (pointer (function (pointer (apply %derived %vtag %tag) %tag) > i32))\n"
        ");\n"
        "\n"
        "%base = recursive (%vtag : upref_type, %tag : upref_type) > (struct\n"
        "  (pointer (apply %vtable %vtag %tag) %vtag)\n"
        "  i32\n"
        ");\n"
        "\n"
        "%derived = recursive (%vtag : upref_type, %tag : upref_type) > (struct\n"
        "  (apply %base (upref %vtag (apply %vtable_derived %vtag %tag) #up0 #up0) (upref %tag (apply %derived %vtag %tag) #up0 #up0))\n"
        "  i32\n"
        ");\n"
        "\n"
        "%func = export function (%obj_wrapped : exists (%vtag : upref_type, %tag : upref_type) > (pointer (apply %derived %vtag %tag) %tag)) > i32 {\n"
        "  %obj = unwrap %obj_wrapped;\n"
        "  %vptr_base = load (gep %obj #up0 #up0 #up0 #up0);\n"
        "  %vptr = outer_ptr (outer_ptr %vptr_base);\n"
        "  %callback1 = load (gep %vptr #up0 #up1);\n"
        "  %callback2 = load (gep %vptr #up0 #up0 #up0 #up0);\n"
        "  %val1 = call %callback1 %obj;\n"
        "  %val2 = call %callback2 %obj;\n"
        "  return (add %val1 %val2);\n"
        "};\n";

      typedef int32_t (*FunctionType) (DerivedDispatchTestObject*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("func", src));
      
      DerivedDispatchTestVtable vtable = {{dispatch_test_callback}, derived_dispatch_test_callback};
      DerivedDispatchTestObject obj = {{(DispatchTestVtable*)&vtable, 10}, 0};
      BOOST_CHECK_EQUAL(f(&obj), 10);
      obj.base.value = 15;
      obj.value = 30;
      BOOST_CHECK_EQUAL(f(&obj), 45);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
