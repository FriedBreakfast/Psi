#include "CodeGenerator.hpp"
#include "Maybe.hpp"

#include <llvm/BasicBlock.h>
#include <llvm/Instructions.h>
#include <llvm/LLVMContext.h>
#include <llvm/Value.h>

namespace Psi {
  namespace Compiler {
    Block::Block(GC::GCPtr<BlockData> data) : m_data(std::move(data)) {
    }

    Function Function::global(std::vector<ParameterType> template_parameters,
                              std::vector<FunctionParameter> parameters,
                              std::vector<FunctionResult> results) {
    }

    Function Function::lambda(std::vector<ParameterType> template_parameters,
                              std::vector<FunctionParameter> parameters,
                              std::vector<FunctionResult> results) {
    }

    void CodeGenerator::block_append(Block& block, Instruction& insn) {
      if (block.m_data->terminated)
        throw std::logic_error("Cannot add instructions to terminated block");

      if (insn.m_ptr->terminator())
        block.m_data->terminated = true;

      insn.m_ptr->check_variables();

      block.m_data->instructions.push_back(*insn.m_ptr.release());
    }

    Block CodeGenerator::block_create_child(Block& block) {
    }

    Value Block::phi(const Type& type) {
    }

    void BlockData::gc_visit(const std::function<bool(GC::Node*)>& visitor) {
      parent.gc_visit(visitor);
      dominator.gc_visit(visitor);

      for (auto it = instructions.begin(); it != instructions.end();) {
        if (visitor(&*it))
          ++it;
        else
          it = instructions.erase(it);
      }
    }

    Block Block::create_child() {
      BlockData *bd = new BlockData;
      bd->terminated = false;
      bd->parent = m_data;
      return Block(bd);
    }

    bool InstructionI::terminator() {
      return false;
    }

    std::vector<Block> InstructionI::jump_targets() {
      return {};
    }
  }
}
