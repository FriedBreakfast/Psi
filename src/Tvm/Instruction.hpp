#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "../Container.hpp"
#include "Core.hpp"

#include <vector>

namespace Psi {
  namespace Tvm {
    class Instruction;
    class PhiNode;
    class Block;

    class Block : public Value {
    public:
      IntrusiveList<Instruction>& instructions() {return m_instructions;}
      IntrusiveList<PhiNode>& phi_nodes() {return m_phi_nodes;}

    private:
      IntrusiveList<Instruction> m_instructions;
      IntrusiveList<PhiNode> m_phi_nodes;

      llvm::BasicBlock *m_llvm_block;
    };

    class PhiNode
      : public Value,
	public IntrusiveListNode<PhiNode> {
    public:
      void add_incoming(Block *bl, Value *v);
    };

    class Instruction
      : public Value,
	public IntrusiveListNode<Instruction> {
    protected:
      enum Slots {
	slot_successor=0,
	slot_max
      };

      /**
       * \param result_type Result type of this instruction.
       */
      Instruction(const UserInitializer& ui, Context *context, Type *result_type);

    private:
      const llvm::Instruction *m_llvm_entry;

      virtual llvm::Value* build_llvm_instructions(llvm::BasicBlock *bl) = 0;

    public:
      virtual ~Instruction();

      Instruction *successor();
      void set_successor(Instruction *i);
    };

#if 0
    /**
     * \brief A set of blocks with a well defined entry and exit point.
     */
    class BlockSequence {
    public:
      BlockSequence();
      BlockSequence(Block *entry, Block *exit);

      Value *value() {return m_value;}
      Block *entry() {return m_entry;}
      Block *exit() {return m_exit;}

      void append(Instruction *i) {exit()->instructions().push_back(i);}
      void extend(const BlockSequence& block);

    private:
      // Result value of this section of code
      Value *m_value;
      // First instruction to execute
      Block *m_entry;
      // Common exit point (usually a no-op)
      Block *m_exit;
    };
#endif

    Instruction* call_instruction(Value *target, std::vector<Value*> arguments);
  }
}

#endif
