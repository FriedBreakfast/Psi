#include "FunctionLower.hpp"

#include <boost/unordered_map.hpp>

namespace Psi {
namespace Compiler {
RewritePass::RewritePass() {
}

RewritePass::~RewritePass() {
}

class RewritePass::Callback {
  RewritePass *m_self;
  TreePtr<Term> m_term;
  
public:
  Callback(RewritePass *self, const TreePtr<Term>& term)
  : m_self(self), m_term(term) {
  }
  
  TreePtr<Term> evaluate(const TreePtr<Term>&) {
    return m_self->derived_apply(m_term);
  }
  
  template<typename Visitor>
  static void visit(Visitor& v) {
    v("term", &Callback::m_term);
  }
};

/**
 * \brief Rewrite a term.
 */
TreePtr<Term> RewritePass::apply(const TreePtr<Term>& term) {
  MapType::iterator it = m_map.find(term);
  if (it != m_map.end())
    return it->second;
  
  TreePtr<Term> result = tree_callback<Term>(term.compile_context(), term.location(), Callback(this, term));
  // Force evaluation
  result.get();
  return result;
}

/**
 * \brief Rewrite a term.
 * 
 * Just a wrapper around apply().
 */
TreePtr<Term> RewritePass::operator () (const TreePtr<Term>& term) {
  return apply(term);
}

FunctionLoweringPass::~FunctionLoweringPass() {
}

struct FunctionLoweringPass::Context {
  Context *parent;
  TreePtr<LoweredFunctionBody> body;
  TreePtr<InstructionBlock> append_block;

  TreePtr<JumpGroup> jump_group;
  std::vector<TreePtr<InstructionBlock> > jump_targets;
  typedef  boost::unordered_map<TreePtr<Statement>, TreePtr<Term> > StatementMapType;
  StatementMapType statements;
  
  Context(const TreePtr<Term>& return_type, const SourceLocation& location)
  : parent(0) {
    body.reset(new LoweredFunctionBody(return_type, location));
    append_block = new_block(location);
  }
  
  Context(Context& parent_)
  : parent(&parent_),
  body(parent_.body),
  append_block(parent_.append_block) {
  }

  TreePtr<Term> lookup(const TreePtr<Statement>& statement) const {
    for (const Context *ptr = this; ptr; ptr = ptr->parent) {
      StatementMapType::const_iterator it = ptr->statements.find(statement);
      if (it != ptr->statements.end())
        return it->second;
    }
  }
  
  TreePtr<InstructionBlock> new_block(const SourceLocation& location) const {
    TreePtr<InstructionBlock> bl(new InstructionBlock(body.compile_context(), location));
    body->blocks.push_back(bl);
    return bl;
  }
};

TreePtr<Term> FunctionLoweringPass::derived_apply(const TreePtr<Term>& term) {
  if (TreePtr<Function> func = dyn_treeptr_cast<Function>(term)) {
    Context context(func->body->type, func->body.location());
    TreePtr<Term> return_value = rewrite_body(context, func->body);
    TreePtr<InstructionReturn> return_insn(new InstructionReturn(return_value, func->body.location()));
    context.append_block->instructions.push_back(return_insn);
    return TreePtr<Function>(new Function(func->result_type, func->arguments, context.body, func.location()));
  } else {
    return term;
  }
}

TreePtr<Term> FunctionLoweringPass::rewrite_body(Context& context, const TreePtr<Term>& term) {
  if (TreePtr<Block> block = dyn_treeptr_cast<Block>(term)) {
    Context my_context(context);
    for (PSI_STD::vector<TreePtr<Statement> >::const_iterator ii = block->statements.begin(), ie = block->statements.end(); ii != ie; ++ii)
      my_context.statements.insert(std::make_pair(*ii, rewrite_body(my_context, (*ii)->value)));
    return rewrite_body(my_context, block->value);
  } else if (TreePtr<IfThenElse> if_then_else = dyn_treeptr_cast<IfThenElse>(term)) {
    TreePtr<Term> cond = rewrite_body(context, if_then_else->condition);
    TreePtr<InstructionBlock> true_block = context.new_block(if_then_else->true_value.location());
    TreePtr<InstructionBlock> false_block = context.new_block(if_then_else->false_value.location());
    TreePtr<InstructionBlock> exit_block = context.new_block(context.append_block.location());

    TreePtr<Instruction> jump(new InstructionJump(cond, true_block, false_block, if_then_else.location()));

    context.append_block->instructions.push_back(jump);
    
    Context true_context(context);
    true_context.append_block = true_block;
    rewrite_body(true_context, if_then_else->true_value);
    TreePtr<Instruction> true_exit(new InstructionGoto(exit_block, if_then_else.location()));
    true_context.append_block->instructions.push_back(true_exit);
    
    Context false_context(context);
    false_context.append_block = false_block;
    rewrite_body(false_context, if_then_else->false_value);
    TreePtr<Instruction> false_exit(new InstructionGoto(exit_block, if_then_else.location()));
    false_context.append_block->instructions.push_back(false_exit);
    
    context.append_block = exit_block;

    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<JumpGroup> jump_group = dyn_treeptr_cast<JumpGroup>(term)) {
    std::size_t n = jump_group->entries.size();
    
    Context my_context(context);
    my_context.jump_group = jump_group;
    for (std::size_t i = 0; i != n; ++i)
      my_context.jump_targets.push_back(my_context.new_block(jump_group->entries[i].location()));

    for (std::size_t i = 0; i != n; ++i) {
      my_context.append_block = my_context.jump_targets[i];
      rewrite_body(my_context, jump_group->entries[i]->value);
    }

    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<JumpTo> jump_to = dyn_treeptr_cast<JumpTo>(term)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<Statement> statement = dyn_treeptr_cast<Statement>(term)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    compile_context().error_throw(term.location(), "Function lowering failed: unknown term type", CompileError::error_internal);
  }
}

LoweredFunctionBody::LoweredFunctionBody(CompileContext& context, const SourceLocation& location)
: Term(&vtable, context, location) {
}

LoweredFunctionBody::LoweredFunctionBody(const TreePtr<Term>& type, const SourceLocation& location)
: Term(&vtable, type, location) {
}

template<typename Visitor>
void LoweredFunctionBody::visit(Visitor& v) {
  visit_base<Term>(v);
}

InstructionBlock::InstructionBlock(CompileContext& context, const SourceLocation& location)
: Term(&vtable, context, location) {
}

template<typename Visitor>
void InstructionBlock::visit(Visitor& v) {
  visit_base<Term>(v);
}

Instruction::Instruction(const TermVtable *vptr, CompileContext& context, const SourceLocation& location)
: Term(vptr, context, location) {
}

Instruction::Instruction(const TermVtable *vptr, const TreePtr<Term>& type, const SourceLocation& location)
: Term(vptr, type, location) {
}

template<typename Visitor>
void Instruction::visit(Visitor& v) {
  visit_base<Term>(v);
}

InstructionAlloca::InstructionAlloca(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

template<typename Visitor>
void InstructionAlloca::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionReturn::InstructionReturn(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

InstructionReturn::InstructionReturn(const TreePtr<Term>& value_, const SourceLocation& location)
: Instruction(&vtable, value_.compile_context().builtins().bottom_type, location),
value(value_) {
}

template<typename Visitor>
void InstructionReturn::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionJump::InstructionJump(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

InstructionJump::InstructionJump(const TreePtr<Term>& condition_, const TreePtr<InstructionBlock>& true_target_, const TreePtr<InstructionBlock>& false_target_, const SourceLocation& location)
: Instruction(&vtable, condition_.compile_context().builtins().bottom_type, location),
condition(condition_),
true_target(true_target_),
false_target(false_target_) {
}

template<typename Visitor>
void InstructionJump::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionGoto::InstructionGoto(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

InstructionGoto::InstructionGoto(const TreePtr<InstructionBlock>& target_, const SourceLocation& location)
: Instruction(&vtable, target.compile_context().builtins().bottom_type, location),
target(target_) {
}

template<typename Visitor>
void InstructionGoto::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionCall::InstructionCall(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

template<typename Visitor>
void InstructionCall::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionStore::InstructionStore(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

template<typename Visitor>
void InstructionStore::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionLoad::InstructionLoad(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

template<typename Visitor>
void InstructionLoad::visit(Visitor& v) {
  visit_base<Instruction>(v);
}

InstructionCompute::InstructionCompute(CompileContext& context, const SourceLocation& location)
: Instruction(&vtable, context, location) {
}

template<typename Visitor>
void InstructionCompute::visit(Visitor& v) {
  visit_base<Instruction>(v);
}


const TermVtable LoweredFunctionBody::vtable = PSI_COMPILER_TERM(LoweredFunctionBody, "psi.compiler.LoweredFunctionBody", Term);
const TermVtable InstructionBlock::vtable = PSI_COMPILER_TERM(InstructionBlock, "psi.compiler.InstructionBlock", Term);
const SIVtable Instruction::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.Instruction", Term);

const TermVtable InstructionAlloca::vtable = PSI_COMPILER_TERM(InstructionAlloca, "psi.compiler.InstructionAlloca", Instruction);
const TermVtable InstructionReturn::vtable = PSI_COMPILER_TERM(InstructionReturn, "psi.compiler.InstructionReturn", Instruction);
const TermVtable InstructionJump::vtable = PSI_COMPILER_TERM(InstructionJump, "psi.compiler.InstructionJump", Instruction);
const TermVtable InstructionGoto::vtable = PSI_COMPILER_TERM(InstructionGoto, "psi.compiler.InstructionGoto", Instruction);
const TermVtable InstructionCall::vtable = PSI_COMPILER_TERM(InstructionCall, "psi.compiler.InstructionCall", Instruction);
const TermVtable InstructionStore::vtable = PSI_COMPILER_TERM(InstructionStore, "psi.compiler.InstructionStore", Instruction);
const TermVtable InstructionLoad::vtable = PSI_COMPILER_TERM(InstructionLoad, "psi.compiler.InstructionLoad", Instruction);
const TermVtable InstructionCompute::vtable = PSI_COMPILER_TERM(InstructionCompute, "psi.compiler.InstructionCompute", Instruction);
}
}
