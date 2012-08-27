#include "Test.hpp"

#include "Aggregate.hpp"
#include "Jit.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(AggregateTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(EmptyStructTest) {
      const char *src = "%es = global const type struct;\n";

      Jit::Metatype *mt = static_cast<Jit::Metatype*>(jit_single("es", src));
      BOOST_CHECK_EQUAL(mt->size, 0);
      BOOST_CHECK_EQUAL(mt->align, 1);
    }
    
    BOOST_AUTO_TEST_CASE(DownUpRefTest) {
      const char *src =
        "%s = define struct i32 i32;\n"
        "%f = function (%a:pointer %s) > (pointer %s) {\n"
        " return (outer_ptr (struct_ep %a #i1));\n"
        "};\n";

      typedef void* (*FunctionType) (void*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      int x[2];
      BOOST_CHECK_EQUAL(f(x), x);
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
        "%vtable = recursive (%tag : upref) > (struct\n"
        "  (pointer (function (pointer (apply %base %tag) %tag) > i32))\n"
        ");\n"
        "\n"
        "%base = recursive (%tag : upref) > (struct\n"
        "  (pointer (apply %vtable %tag))\n"
        ");\n"
        "\n"
        "%func = function (%obj_wrapped : exists (%tag : member) > (pointer (apply %base %tag) %tag)) > i32 {\n"
        "  %obj = unwrap %obj_wrapped;\n"
        "  %tag = unwrap_param %obj_wrapped #i0;\n"
        "  %vptr = load (struct_ep %obj #i0);\n"
        "  %callback = load (struct_ep %vptr #i0);\n"
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

    BOOST_AUTO_TEST_SUITE_END()
  }
}
