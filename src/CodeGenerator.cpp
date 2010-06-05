#include "CodeGenerator.hpp"
#include "Maybe.hpp"

#include <llvm/BasicBlock.h>
#include <llvm/Instructions.h>
#include <llvm/LLVMContext.h>
#include <llvm/Value.h>

namespace Psi {
  namespace Compiler {
    InstructionList::InstructionList() {
    }

    InstructionList::InstructionList(InstructionList&& src) {
      append(src);
    }

    InstructionList::~InstructionList() {
    }

    void InstructionList::append(InstructionList&& src) {
      append(src);
    }

    void InstructionList::append(InstructionList& src) {
      m_list.splice(m_list.end(), src.m_list);
    }

#if 0
    struct Context {
      llvm::LLVMContext llvm_context;
      /// This will always be i8*
      llvm::Type *generic_type;
      std::unordered_map<TypeSystem::Constructor, llvm::Type*> simple_types;
    };

    std::shared_ptr<Context> make_context() {
      auto sp = std::make_shared<Context>();
      sp->generic_type = llvm::PointerType::getUnqual(llvm::IntegerType::get(sp->llvm_context, 8));
      return sp;
    }

    struct Value::Data {
      llvm::Value *value;
      TypeSystem::Type type;
      bool destroyed;
    };

    struct Block::Data {
      /// Immediate predecessor blocks.
      std::vector<Block> predecessors;
      /// Block which dominates this one.
      Block dominator;
      /// LLVM block
      llvm::BasicBlock *block;
      /// Context which generated this block
      std::shared_ptr<Context> context;
    };

    struct PhiNode::Data {
      Block block;
      llvm::PHINode *phi_node;
      std::vector<std::pair<llvm::Value*, llvm::BasicBlock*> > entries;
      Maybe<TypeSystem::Type> type;
      Maybe<Value> value;
    };

    namespace {
      llvm::Type* to_llvm_type(const Context& con, const TypeSystem::Type& ty) {
        auto simple = ty.as_primitive();
        if (simple) {
          auto it = con.simple_types.find(*simple);
          if (it != con.simple_types.end())
            return it->second;
        }

        return con.generic_type;
      }
    }

    PhiNode::PhiNode(Block block) : m_data(std::make_shared<Data>(Data{std::move(block), 0, {}, {}, {}})) {
    }

    void PhiNode::merge(const Block& incoming, const Value& value) {
      // Work out result type
      if (m_data->type) {
        std::abort();
      } else {
        m_data->type = value.m_data->type;
      }

      if (m_data->phi_node) {
        assert(m_data->type);
        m_data->phi_node->addIncoming(value.m_data->value, incoming.m_data->block);
      } else {
        m_data->entries.push_back({value.m_data->value, incoming.m_data->block});
      }
    }

    const Value& PhiNode::value() {
      if (!m_data->value) {
        if (!m_data->type)
          throw std::logic_error("Cannot get a value for a Phi node with no entries");

        llvm::Type *llvm_type = to_llvm_type(*m_data->block.m_data->context, *m_data->type);
        m_data->phi_node = llvm::PHINode::Create(llvm_type);
        m_data->block.m_data->block->getInstList().push_front(m_data->phi_node);

        for (auto it = m_data->entries.begin(); it != m_data->entries.end(); ++it)
          m_data->phi_node->addIncoming(it->first, it->second);

        m_data->value = Value{std::make_shared<Value::Data>(Value::Data{m_data->phi_node, std::move(*m_data->type), false})};
      }

      return *m_data->value;
    }

    BlockGenerator::BlockGenerator(Block block) : m_block(std::move(block)) {
    }

    PhiNode BlockGenerator::phi() {
      return PhiNode(m_block);
    }

    std::vector<Value> BlockGenerator::invoke(const Function& fnuction, const std::vector<Value>& arguments) {
    }

    void BlockGenerator::destroy(Value& value) {
      if (value.m_data->destroyed)
        throw std::logic_error("value has already been destroyed");

      value.m_data->destroyed = true;
      std::abort();
    }

    void BlockGenerator::branch(const Value& cond, Block& if_true, Block& if_false) {
      if (!llvm::BranchInst::Create(if_true.m_data->block, if_false.m_data->block, cond.m_data->value, m_block.m_data->block))
        throw std::runtime_error("Failed to create branch instruction");
    }

    void BlockGenerator::goto_(Block& target) {
      if (!llvm::BranchInst::Create(target.m_data->block, m_block.m_data->block))
        throw std::runtime_error("Failed to create branch instruction");
    }
#endif
  }
}
