#include "Instruction.hpp"

namespace Psi {
  CodeBlock::CodeBlock() : m_entry(NULL), m_exit(NULL) {
  }

  CodeBlock::CodeBlock(Instruction *i) : m_entry(i), m_exit(i) {
  }

  void CodeBlock::append(Instruction *i) {
    m_exit->set_successor(i);
    m_exit = i;
  }

  void CodeBlock::extend(const CodeBlock& bl) {
    m_exit->set_successor(bl.m_entry);
    m_exit = bl.m_exit;
  }

  CodeValue::CodeValue() : m_value(NULL) {
  }

  CodeValue::CodeValue(Instruction *i) : m_value(i->result()), m_block(i) {
  }

  CodeValue::CodeValue(Value *v, const CodeBlock& bl) : m_value(v), m_block(bl) {
  }

  void CodeValue::append(Instruction *i) {
    m_block.append(i);
  }

  void CodeValue::set_value(Value *v) {
    m_value = v;
  }

  void CodeValue::extend(const CodeValue& v) {
    m_block.extend(v.m_block);
  }

  void CodeValue::extend_replace(const CodeValue& v) {
    m_block.extend(v.m_block);
    m_value = v.m_value;
  }

  Instruction* Instruction::successor() {
    InstructionLabelValue *v = use_get<InstructionLabelValue>(slot_successor);
    return v ? v->instruction() : NULL;
  }

  void Instruction::set_successor(Instruction *i) {
    use_set(slot_successor, i->label());
  }

  Instruction* InstructionResultValue::instruction() {
    return reverse_member_lookup(this, &Instruction::m_result_value);
  }

  Instruction* InstructionLabelValue::instruction() {
    return reverse_member_lookup(this, &Instruction::m_label_value);
  }

  Instruction* InstructionContextValue::instruction() {
    return reverse_member_lookup(this, &Instruction::m_context_value);
  }

  llvm::Value* InstructionResultValue::build_llvm_value(llvm::LLVMContext& context) {
    throw std::logic_error("not implemented");
  }

  llvm::Type* InstructionResultValue::build_llvm_type(llvm::LLVMContext& context) {
    throw std::logic_error("not implemented");
  }

  llvm::Value* InstructionLabelValue::build_llvm_value(llvm::LLVMContext& context) {
    throw std::logic_error("not implemented");
  }

  llvm::Type* InstructionLabelValue::build_llvm_type(llvm::LLVMContext& context) {
    throw std::logic_error("not implemented");
  }

  llvm::Value* InstructionContextValue::build_llvm_value(llvm::LLVMContext& context) {
    throw std::logic_error("not implemented");
  }

  llvm::Type* InstructionContextValue::build_llvm_type(llvm::LLVMContext& context) {
    throw std::logic_error("not implemented");
  }
}
