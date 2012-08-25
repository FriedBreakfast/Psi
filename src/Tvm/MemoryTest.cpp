#include "Test.hpp"

#include "Function.hpp"
#include "Memory.hpp"
#include "JitTypes.hpp"

namespace Psi {
  namespace Tvm {
    /*
     * Test that construction and destruction of the context
     * without leaking memory works.
     */
    BOOST_FIXTURE_TEST_SUITE(MemoryTest, Test::ContextFixture)
    BOOST_AUTO_TEST_SUITE_END()
  }
}
