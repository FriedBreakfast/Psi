#include "Compiler.hpp"
#include "Tree.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/InstructionBuilder.hpp"
#include "Tvm/FunctionalBuilder.hpp"

namespace Psi {
namespace Compiler {
class TvmFunctionLoweringContext {
public:
  struct JumpTargetData {
    Tvm::ValuePtr<Tvm::Block> block;
    Tvm::ValuePtr<Tvm::Phi> argument_phi;
    Tvm::ValuePtr<> argument_slot;
  };
  
private:
  TvmFunctionLoweringContext *m_parent;
  CompileContext *m_compile_context;
  Tvm::InstructionBuilder m_builder;

  typedef boost::unordered_map<TreePtr<JumpGroupEntry>, JumpTargetData> JumpTargetMap;
  JumpTargetMap m_jump_targets;
  Tvm::ValuePtr<> m_jump_storage;

  typedef boost::unordered_map<TreePtr<Statement>, Tvm::ValuePtr<> > StatementMapType;
  StatementMapType m_statements;

public:
  TvmFunctionLoweringContext(TvmFunctionLoweringContext *parent,
                             const Tvm::ValuePtr<Tvm::Block>& insert_point=Tvm::ValuePtr<Tvm::Block>(),
                             CompileContext *compile_context=NULL);
  
  CompileContext& compile_context() {return *m_compile_context;}
  Tvm::Context& tvm_context();
  
  /// \brief Detect whether this term is of bottom type, implying that its evaluation will exit abnormally.
  bool is_bottom(const TreePtr<Term>& term);
  /// \brief Detect whether a type is primitive so may live in registers rather than on the stack/heap.
  bool is_primitive(const TreePtr<Term>& type);
  
  /// Generate default constructor call
  void empty_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest);
  /// Generate move constructor call
  void move_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src);
  /// Generate a move constructor call followed by a destructor call on the source.
  void move_construct_destroy(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src);
  /// Generate destructor call
  void destroy(const TreePtr<Term>& type, const Tvm::ValuePtr<>& ptr);
  
  Tvm::ValuePtr<> run_root(const TreePtr<Term>& term, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope);
  Tvm::ValuePtr<> run(const TreePtr<Term>& term, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope);
  Tvm::ValuePtr<> run_if_then_else(const TreePtr<IfThenElse>& if_then_else, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope);
  Tvm::ValuePtr<> run_jump_group(const TreePtr<JumpGroup>& jump_group, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope);
  Tvm::ValuePtr<> run_jump(const TreePtr<JumpTo>& jump_to, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope);
  
  Tvm::ValuePtr<> run_type(const TreePtr<Term>& type);

  void scope_cleanup(TvmFunctionLoweringContext *following_scope);
  
  Tvm::InstructionBuilder& builder() {return m_builder;}
};

/**
 * \class TvmFunctionLoweringContext
 * 
 * Converts a Function to a Tvm::Function.
 * 
 * Variable lifecycles are tracked by "following scope" pointers. The scope which will
 * be current immediately after the current term is tracked, so that variables which
 * are about to go out of scope can be detected.
 */

TvmFunctionLoweringContext::TvmFunctionLoweringContext(TvmFunctionLoweringContext *parent,
                                                       const Tvm::ValuePtr<Tvm::Block>& insert_point,
                                                       CompileContext *compile_context)
: m_parent(parent),
m_compile_context(compile_context ? compile_context : parent->m_compile_context),
m_builder(insert_point ? Tvm::InstructionInsertPoint(insert_point) : parent->builder().insert_point()) {
}

/**
 * Run the highest level of a context. In this function new local variables (in Blocks) may be created
 * in this context, rather than a child context being created to hold them.
 * 
 * \param following_scope Scope which will apply immediately after this call. This allows variables
 * which are about to go out of scope to be detected.
 */
Tvm::ValuePtr<> TvmFunctionLoweringContext::run_root(const TreePtr<Term>& term, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope) {
  if (TreePtr<Block> block = dyn_treeptr_cast<Block>(term)) {
    for (PSI_STD::vector<TreePtr<Statement> >::const_iterator ii = block->statements.begin(), ie = block->statements.end(); ii != ie; ++ii) {
      Tvm::ValuePtr<> child_slot = builder().alloca_(run_type(term->type), ii->location());
      m_statements.insert(std::make_pair(*ii, run((*ii)->value, child_slot, this)));
    }
    
    return run(block->value, slot, following_scope);
  } else {
    return run(term, slot, following_scope);
  }
}

Tvm::ValuePtr<> TvmFunctionLoweringContext::run(const TreePtr<Term>& term, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope) {
  PSI_ASSERT(is_primitive(term->type) == bool(slot));

  if (TreePtr<Block> block = dyn_treeptr_cast<Block>(term)) {
    return TvmFunctionLoweringContext(this).run_root(block, slot, following_scope);
  } else if (TreePtr<IfThenElse> if_then_else = dyn_treeptr_cast<IfThenElse>(term)) {
    return run_if_then_else(if_then_else, slot, following_scope);
  } else if (TreePtr<JumpGroup> jump_group = dyn_treeptr_cast<JumpGroup>(term)) {
    return TvmFunctionLoweringContext(this).run_jump_group(jump_group, slot, following_scope);
  } else if (TreePtr<JumpTo> jump_to = dyn_treeptr_cast<JumpTo>(term)) {
    return run_jump(jump_to, slot, following_scope);
  } else if (TreePtr<Statement> statement = dyn_treeptr_cast<Statement>(term)) {
    Tvm::ValuePtr<> value;
    
    for (const TvmFunctionLoweringContext *ctx = this; ctx; ctx = ctx->m_parent) {
      StatementMapType::const_iterator ii = ctx->m_statements.find(statement);
      if (ii != ctx->m_statements.end()) {
        value = ii->second;
        break; 
      }
    }
    
    if (!value)
      compile_context().error_throw(statement->location(), "Variable not in scope");
    
    if (is_primitive(statement->type)) {
      return value;
    } else {
      // Move, copy or reference?
      PSI_NOT_IMPLEMENTED();
    }
  } else {
    compile_context().error_throw(term.location(), "Function lowering failed: unknown term type", CompileError::error_internal);
  }
}

/**
 * \brief Create TVM structure for If-Then-Else.
 */
Tvm::ValuePtr<> TvmFunctionLoweringContext::run_if_then_else(const TreePtr<IfThenElse>& if_then_else, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope) {
  Tvm::ValuePtr<> cond = run(if_then_else->condition, Tvm::ValuePtr<>(), this);
  Tvm::ValuePtr<Tvm::Block> true_block = builder().new_block(if_then_else->true_value.location());
  Tvm::ValuePtr<Tvm::Block> false_block = builder().new_block(if_then_else->false_value.location());

  builder().cond_br(cond, true_block, false_block, if_then_else.location());
  
  TvmFunctionLoweringContext true_context(this, true_block);
  Tvm::ValuePtr<> true_value = true_context.run_root(if_then_else->true_value, slot, following_scope);
  
  TvmFunctionLoweringContext false_context(this, false_block);
  Tvm::ValuePtr<> false_value = false_context.run_root(if_then_else->false_value, slot, following_scope);
  
  if (!is_bottom(if_then_else->true_value) && !is_bottom(if_then_else->false_value)) {
    Tvm::ValuePtr<Tvm::Block> exit_block = builder().new_block(if_then_else.location());
    true_context.builder().br(exit_block, if_then_else.location());
    false_context.builder().br(exit_block, if_then_else.location());
    builder().set_insert_point(exit_block);

    if (!slot) {
      // This is a primitive value so merge through a phi node
      Tvm::ValuePtr<Tvm::Phi> phi = builder().phi(true_value->type(), if_then_else.location());
      phi->add_edge(true_context.builder().block(), true_value);
      phi->add_edge(false_context.builder().block(), false_value);
      return phi;
    } else {
      return Tvm::ValuePtr<>();
    }
  } else if (!is_bottom(if_then_else->true_value)) {
    builder().set_insert_point(true_block);
    return true_value;
  } else if (!is_bottom(if_then_else->false_value)) {
    builder().set_insert_point(false_block);
    return false_value;
  } else {
    return Tvm::ValuePtr<>();
  }
}

/**
 * \brief Create TVM structure for a jump group.
 */
Tvm::ValuePtr<> TvmFunctionLoweringContext::run_jump_group(const TreePtr<JumpGroup>& jump_group, const Tvm::ValuePtr<>& slot, TvmFunctionLoweringContext *following_scope) {
  PSI_ASSERT(m_jump_targets.empty());
  
  std::vector<Tvm::ValuePtr<> > parameter_types;
  for (std::vector<TreePtr<JumpGroupEntry> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    JumpTargetData data;
    data.block = builder().new_block(ii->location());
    if ((*ii)->argument) {
      Tvm::InstructionBuilder child_builder(data.block);
      if (is_primitive((*ii)->argument->type))
        data.argument_phi = child_builder.phi(Tvm::ValuePtr<>(), ii->location());
      else
        parameter_types.push_back(run_type((*ii)->argument->type));
    }
    m_jump_targets.insert(std::make_pair(*ii, data));
  }

  // Construct a union of all jump target parameter types,
  // in order to allocate the least storage possible.
  if (!parameter_types.empty())
    m_jump_storage = builder().alloca_(Tvm::FunctionalBuilder::union_type(tvm_context(), parameter_types, jump_group.location()), jump_group.location());

  std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, Tvm::ValuePtr<> > > non_bottom_values;
  
  Tvm::ValuePtr<> initial_value = run_root(jump_group->initial, slot, following_scope);
  if (!is_bottom(jump_group->initial))
    non_bottom_values.push_back(std::make_pair(builder().block(), initial_value));

  for (JumpTargetMap::const_iterator ii = m_jump_targets.begin(), ie = m_jump_targets.end(); ii != ie; ++ii) {
    TvmFunctionLoweringContext target_context(this, ii->second.block);
    Tvm::ValuePtr<> value = target_context.run_root(ii->first->value, slot, following_scope);
    if (!is_bottom(ii->first->value))
      non_bottom_values.push_back(std::make_pair(target_context.builder().block(), value));
  }
  
  if (non_bottom_values.size() > 1) {
    Tvm::ValuePtr<Tvm::Block> exit_block = builder().new_block(jump_group.location());
    for (std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, Tvm::ValuePtr<> > >::iterator ii = non_bottom_values.begin(), ie = non_bottom_values.end(); ii != ie; ++ii)
      Tvm::InstructionBuilder(ii->first).br(exit_block, jump_group.location());
    
    builder().set_insert_point(exit_block);
    if (!slot) {
      Tvm::ValuePtr<Tvm::Phi> phi = builder().phi(non_bottom_values.front().second->type(), jump_group.location());
      for (std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, Tvm::ValuePtr<> > >::iterator ii = non_bottom_values.begin(), ie = non_bottom_values.end(); ii != ie; ++ii)
        phi->add_edge(ii->first, ii->second);
      return phi;
    } else {
      return Tvm::ValuePtr<>();
    }
  } else if (non_bottom_values.size() == 1) {
    builder().set_insert_point(non_bottom_values.front().first);
    return non_bottom_values.front().second;
  } else {
    return Tvm::ValuePtr<>();
  }
}

/**
 * Handle a jump.
 */
Tvm::ValuePtr<> TvmFunctionLoweringContext::run_jump(const TreePtr<JumpTo>& jump_to, const Tvm::ValuePtr<>&, TvmFunctionLoweringContext *following_scope) {
  JumpTargetData jump_data;
  TvmFunctionLoweringContext *jump_scope = this;
  for (; jump_scope; jump_scope = jump_scope->m_parent) {
    JumpTargetMap::const_iterator ii = jump_scope->m_jump_targets.find(jump_to->target);
    if (ii != jump_scope->m_jump_targets.end()) {
      jump_data = ii->second;
      break;
    }
  }
  
  if (!jump_scope)
    compile_context().error_throw(jump_to->location(), "Target of jump is not in scope");

  Tvm::ValuePtr<> argument_temp, argument_type;
  if (jump_to->argument) {
    Tvm::ValuePtr<> arg = run(jump_to->argument, Tvm::ValuePtr<>(), jump_scope);
    if (is_primitive(jump_to->argument->type)) {
      jump_data.argument_phi->add_edge(builder().block(), arg);
    } else {
      // Need to move through a temporary copy to ensure that the original storage isn't
      // part of the object being moved from, since I'm not tracking object origins.
      argument_type = run_type(jump_to->argument->type);
      argument_temp = builder().alloca_(argument_type, jump_to.location());
      move_construct(jump_to->argument->type, argument_temp, arg);
    }
  }
  
  builder().br(jump_data.block, jump_to.location());
  
  scope_cleanup(following_scope);
  
  if (argument_temp) {
    // If we have used a temporary to hold the jump argument,
    // now move it into its final storage location.
    Tvm::ValuePtr<> argument_dest = Tvm::FunctionalBuilder::union_element_ptr(jump_scope->m_jump_storage, run_type(jump_to->argument->type), jump_to.location());
    move_construct(jump_to->argument->type, argument_dest, argument_temp);
    destroy(jump_to->argument->type, argument_temp);
  }
  
  return Tvm::ValuePtr<>();
}

/**
 * \brief Perform scope cleanup, destroying local objects.
 * 
 * This will generate destructor calls to objects generated in scopes between the current
 * one and \c following_scope, so that afterwards only objects in \c following_scope or
 * higher still exist.
 * 
 * \param following_scope Scope which will exist immediately following
 * this call.
 */
void TvmFunctionLoweringContext::scope_cleanup(TvmFunctionLoweringContext *following_scope) {
  PSI_NOT_IMPLEMENTED();
  
  for (TvmFunctionLoweringContext *scope = this; scope != following_scope; scope = scope->m_parent) {
    PSI_ASSERT(scope != NULL);
  }
} 
}
}