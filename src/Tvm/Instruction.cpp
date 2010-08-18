#include "Instruction.hpp"

namespace Psi {
  namespace Tvm {
    Instruction::Instruction(const UserInitializer& ui, Context *context, Type *result_type)
      : Value(ui, context, result_type, false, false) {
    }

    Instruction::~Instruction() {
    }
  }
}
