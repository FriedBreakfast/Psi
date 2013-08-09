#ifndef HPP_PSI_TVM_TEST
#define HPP_PSI_TVM_TEST

#include "../ErrorContext.hpp"
#include "../Test.hpp"
#include "Core.hpp"
#include "Jit.hpp"

#include <boost/shared_ptr.hpp>

namespace Psi {
  namespace Tvm {
    namespace Test {
      class ContextFixture {
      public:
        SourceLocation location;
        CompileErrorContext error_context;
        Context context;
        Module module;

        ContextFixture();
        ~ContextFixture();

        void* jit_single(const char *name, const char *src);

      private:
        class DebugListener;
        boost::shared_ptr<Jit> m_jit;
        boost::shared_ptr<DebugListener> m_debug_listener;
      };
    }
  }
}

#endif
