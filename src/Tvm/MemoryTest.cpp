#include "Test.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(MemoryTest, Test::ContextFixture)
    
    /*
     * Test that construction and destruction of the context
     * without leaking memory works.
     */
    BOOST_AUTO_TEST_CASE(ContextTest) {
    }
    
    BOOST_AUTO_TEST_SUITE_END()
  }
}
