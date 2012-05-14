#include "Jit.hpp"

/*
 * Do not remove the JIT.h include. Although everything will build
 * fine, the JIT will not be available since JIT.h includes some magic
 * which ensures the JIT is really available.
 */
#include <llvm/ExecutionEngine/JIT.h>

namespace Psi {
namespace Compiler {
namespace LLVMBuilder {
}
}
}
