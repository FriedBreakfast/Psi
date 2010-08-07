#ifndef HPP_PSI_CODEGENERATOR
#define HPP_PSI_CODEGENERATOR

#include "Value.hpp"

#include <llvm/Instruction.h>

#include <vector>

namespace Psi {
  class Instruction;

  class InstructionValue : public Value {
    friend class Instruction;

  public:
    /**
     * Get the instruction associated with this value. The exact value
     * represented is defined by the subclass of InstructionValue.
     */
    virtual Instruction* instruction() = 0;

  private:
    InstructionValue() {
      init_uses(m_uses);
    }

    StaticUses<Value::slot_max> m_uses;
  };

  class InstructionResultValue : public InstructionValue {
  public:
    virtual Instruction *instruction();

  private:
    virtual llvm::Value* build_llvm_value(llvm::LLVMContext& context);
    virtual llvm::Type* build_llvm_type(llvm::LLVMContext& context);
  };

  class InstructionLabelValue : public InstructionValue {
  public:
    virtual Instruction *instruction();

  private:
    virtual llvm::Value* build_llvm_value(llvm::LLVMContext& context);
    virtual llvm::Type* build_llvm_type(llvm::LLVMContext& context);
  };

  class InstructionContextValue : public InstructionValue {
  public:
    virtual Instruction *instruction();

  private:
    virtual llvm::Value* build_llvm_value(llvm::LLVMContext& context);
    virtual llvm::Type* build_llvm_type(llvm::LLVMContext& context);
  };

  class Instruction : public User {
  protected:
    enum Slots {
      slot_successor=0,
      slot_max
    };

  private:
    Type *m_result_type;

    friend Instruction* InstructionResultValue::instruction();
    friend Instruction* InstructionLabelValue::instruction();
    friend Instruction* InstructionContextValue::instruction();

    InstructionResultValue m_result_value;
    InstructionLabelValue m_label_value;
    InstructionContextValue m_context_value;

  public:
    virtual ~Instruction();

    virtual llvm::Instruction* to_llvm() = 0;

    Value* result() {return &m_result_value;}
    Value* label() {return &m_label_value;}
    Value* context() {return &m_context_value;}

    Instruction *successor();
    void set_successor(Instruction *i);
  };

  /**
   * \brief A sequence of instructions.
   */
  class CodeBlock {
  public:
    CodeBlock();
    CodeBlock(Instruction *i);

    Instruction *entry() {return m_entry;}
    Instruction *exit() {return m_exit;}

    void append(Instruction *i);
    void extend(const CodeBlock& block);

  private:
    // First instruction to execute
    Instruction *m_entry;
    // Common exit point (usually a no-op)
    Instruction *m_exit;
  };

  class CodeValue {
  public:
    CodeValue();
    CodeValue(Instruction *i);
    CodeValue(Value *v, const CodeBlock& bl);

    Value *value() {return m_value;}
    CodeBlock& block() {return m_block;}

    void append(Instruction *i);
    void set_value(Value *v);
    void extend(const CodeValue& v);
    void extend_replace(const CodeValue& v);

  private:
    // Value of this block
    Value *m_value;
    // Block
    CodeBlock m_block;
  };

  Instruction* call_instruction(Value *target, std::vector<Value*> arguments);
}

#endif
