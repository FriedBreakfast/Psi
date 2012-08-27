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
        " return (outer_ptr (struct_ep %a #i0));\n"
        "};\n";

      typedef void* (*FunctionType) (void*);
      FunctionType f = reinterpret_cast<FunctionType>(jit_single("f", src));

      int x[2];
      BOOST_CHECK_EQUAL(f(x), x);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
