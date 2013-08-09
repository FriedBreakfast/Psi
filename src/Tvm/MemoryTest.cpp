#include "Test.hpp"

namespace Psi {
  namespace Tvm {
    PSI_TEST_SUITE_FIXTURE(MemoryTest, Test::ContextFixture)
    
    /*
     * Test that construction and destruction of the context
     * without leaking memory works.
     */
    PSI_TEST_CASE(ContextTest) {
    }
    
    PSI_TEST_SUITE_END()
  }
}
