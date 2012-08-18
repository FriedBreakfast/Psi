#include "Compiler.hpp"
#include "Tree.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/InstructionBuilder.hpp"
#include "Tvm/FunctionalBuilder.hpp"

namespace Psi {
namespace Compiler {
enum LocalStorage {
  /// \brief Functional value.
  local_functional,
  /// \brief Normal object allocated on the stack.
  local_stack,
  /// \brief Reference to L-value.
  local_lvalue_ref,
  /**
    * \brief Reference to R-value.
    * 
    * Note that this only behaves as an R-value reference when it is about
    * to go out of scope, otherwise it behaves as an L-value.
    */
  local_rvalue_ref
};

class FunctionLowering {
  CompileContext *m_compile_context;

  Tvm::ValuePtr<Tvm::Function> m_output;
  TreePtr<JumpTarget> m_return_target;
  Tvm::ValuePtr<> m_return_storage;
  Tvm::InstructionBuilder m_builder;
  
  class Scope;

  std::pair<Scope*, Tvm::ValuePtr<> > exit_info(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location);
  void exit_to(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);
  
  class Variable {
    friend class Scope;
    
    TreePtr<Term> m_type;
    LocalStorage m_storage;
    Tvm::ValuePtr<> m_value;

  public:
    Variable() {}
    Variable(Scope& parent_scope, const TreePtr<Term>& value);
    Variable(Scope& parent_scope, const TreePtr<JumpTarget>& jump_target, const Tvm::ValuePtr<>& stack);

    LocalStorage storage() const {return m_storage;}
    
    /// \brief Assign a functional value to this variable.
    void assign(const Tvm::ValuePtr<>& value);

    const Tvm::ValuePtr<>& value() const {return m_value;}
    const TreePtr<Term>& type() const {return m_type;}
  };

  typedef boost::unordered_map<TreePtr<Statement>, const Variable*> VariableMap;
  VariableMap m_variables;

  class Scope {
    friend std::pair<Scope*, Tvm::ValuePtr<> > FunctionLowering::exit_info(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location);
    friend void FunctionLowering::exit_to(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);

    struct JumpData {
      Tvm::ValuePtr<Tvm::Block> block;
      Scope *scope;
      
      /**
       * Holds either a stack pointer if the jump target parameter is stored on the stack,
       * or a reference to a PHI node.
       */
      Tvm::ValuePtr<> storage;
    };
    
    Scope *m_parent;
    Tvm::ValuePtr<Tvm::Block> m_dominator;
    FunctionLowering *m_shared;
    
    typedef std::map<TreePtr<JumpTarget>, JumpData> JumpMapType;
    /**
     * \brief Jumps out of this context which have already been built, plus jumps into immediate child scopes.
     * 
     * Note that a NULL JumpTarget represents an exception 
     */
    JumpMapType m_jump_map;
    
    Variable m_variable;

    /// Location of this scope
    SourceLocation m_location;
    /// Term the variable corresponds to
    TreePtr<Statement> m_statement;

  public:
    Scope(FunctionLowering *shared, const SourceLocation& location);
    Scope(Scope& parent, const Variable& variable, const SourceLocation& location, const TreePtr<Statement>& statement=TreePtr<Statement>());
    Scope(Scope& parent, const std::map<TreePtr<JumpTarget>, Tvm::ValuePtr<Tvm::Block> >& initial_jump_map, const Tvm::ValuePtr<>& storage, const SourceLocation& location);
    ~Scope();
    
    CompileContext& compile_context() {return m_shared->compile_context();}

    FunctionLowering& shared() const {return *m_shared;}
    Scope *parent() {return m_parent;}
    const Tvm::ValuePtr<Tvm::Block>& dominator() const {return m_dominator;}
    const SourceLocation& location() const {return m_location;}
    Variable& variable() {return m_variable;}
  };

  CompileContext& compile_context() {return *m_compile_context;}
  Tvm::Context& tvm_context();

  LocalStorage storage(const TreePtr<Term>& term);
  bool is_bottom(const TreePtr<Term>& term);
  bool is_primitive(const TreePtr<Term>& type);
  Tvm::ValuePtr<> variable_assign(Scope& scope, const Variable& dest, const Variable& src, Scope& following_scope, const SourceLocation& location);
  bool going_out_of_scope(Scope& scope, const Variable& var, Scope& following_scope);
  
  /// Generate default constructor call
  void empty_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const SourceLocation& location);
  /// Generate copy constructor call
  void copy_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  /// Generate move constructor call
  void move_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  /// Generate a move constructor call followed by a destructor call on the source.
  void move_construct_destroy(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  /// Generate destructor call
  void destroy(const TreePtr<Term>& type, const Tvm::ValuePtr<>& ptr, const SourceLocation& location);
  
  Tvm::ValuePtr<> allocate_slot(const TreePtr<Term>& term);
  Tvm::ValuePtr<> as_functional(const Variable& var, const SourceLocation& location);
  
  Tvm::ValuePtr<> run(Scope& scope, const TreePtr<Term>& term, const Variable& slot, Scope& following_scope);
  Tvm::ValuePtr<> run_block(Scope& scope, const TreePtr<Block>& block, const Variable& slot, Scope& following_scope, unsigned index);
  Tvm::ValuePtr<> run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const Variable& slot, Scope& following_scope);
  Tvm::ValuePtr<> run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const Variable& slot, Scope& following_scope);
  Tvm::ValuePtr<> run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const Variable& slot, Scope& following_scope);
  
  Tvm::ValuePtr<> run_type(const TreePtr<Term>& type);

  /**
   * \brief Get the instruction builder for this function.
   * 
   * Note that the insertion pointer of this builder is modified throughout
   * the lowering process, and its state must be maintained carefully.
   */
  Tvm::InstructionBuilder& builder() {return m_builder;}
};

FunctionLowering::Variable::Variable(Scope& parent_scope, const TreePtr<Term>& value)
: m_type(value->type),
m_storage(parent_scope.shared().storage(value)) {
  if (m_storage == local_stack)
    m_value = parent_scope.shared().builder().alloca_(parent_scope.shared().run_type(m_type), value->location());
}

FunctionLowering::Variable::Variable(Scope& parent_scope, const TreePtr<JumpTarget>& jump_target, const Tvm::ValuePtr<>& stack)
: m_type(jump_target->argument->type) {
  PSI_NOT_IMPLEMENTED(); // Determine storage type from jump target
  if (m_storage == local_stack)
    m_value = stack;
}

void FunctionLowering::Variable::assign(const Tvm::ValuePtr<>& value) {
  PSI_ASSERT(bool(value) == (m_storage == local_functional));
  if (value)
    m_value = value;
}

FunctionLowering::Scope::Scope(FunctionLowering *shared, const SourceLocation& location)
: m_parent(NULL),
m_shared(shared),
m_location(location) {
}

FunctionLowering::Scope::Scope(Scope& parent, const Variable& variable, const SourceLocation& location, const TreePtr<Statement>& statement)
: m_parent(&parent),
m_dominator(parent.m_shared->builder().block()),
m_shared(parent.m_shared),
m_variable(variable),
m_location(location) {
  if (m_statement) {
    if (!m_shared->m_variables.insert(std::make_pair(statement, &m_variable)).second)
      compile_context().error(statement->location(), "Statemen appears in more than one block.");
  }
}

/**
 * \brief Create a new scope with a jump map.
 * 
 * \param initial_jump_map Initial jump map (more entries are generated
 * as required). This object is cleared by this function.
 */
FunctionLowering::Scope::Scope(Scope& parent, const std::map<TreePtr<JumpTarget>, Tvm::ValuePtr<Tvm::Block> >& initial_jump_map, const Tvm::ValuePtr<>& storage, const SourceLocation& location)
: m_parent(&parent),
m_shared(parent.m_shared),
m_location(location) {
  for (std::map<TreePtr<JumpTarget>, Tvm::ValuePtr<Tvm::Block> >::const_iterator ii = initial_jump_map.begin(), ie = initial_jump_map.end(); ii != ie; ++ii) {
    JumpData jd;
    jd.block = ii->second;
    jd.scope = this;
    jd.storage = storage;
    m_jump_map.insert(std::make_pair(ii->first, jd));
  }
}

FunctionLowering::Scope::~Scope() {
  if (m_statement) {
    VariableMap::iterator it = m_shared->m_variables.find(m_statement);
    PSI_ASSERT(it != m_shared->m_variables.end());
    m_shared->m_variables.erase(it);
  }
}

/**
 * \brief Get some information about jumping to a specified target from a scope.
 * 
 * This returns the scope of the jump target plus the memory to be used to store
 * the jup argument if the argument is stored on the stack. If the argument
 * is passed through a funtional value or reference the second result must be
 * ignored.
 * 
 * Note that for re-throwing the final scope is always NULL and no data is passed,
 * so this function should not be used.
 */
std::pair<FunctionLowering::Scope*, Tvm::ValuePtr<> > FunctionLowering::exit_info(Scope& from, const TreePtr<JumpTarget>& target, const SourceLocation& location) {
  PSI_ASSERT(target);
  
  if (target == m_return_target) {
    // Find root scope
    Scope *root_scope;
    for (root_scope = &from; root_scope->parent(); root_scope = root_scope->parent());
    return std::make_pair(root_scope, m_return_storage);
  } else {
    for (Scope *scope = &from; ; scope = scope->parent()) {
      Scope::JumpMapType::const_iterator it = scope->m_jump_map.find(target);
      if (it != scope->m_jump_map.end())
        return std::make_pair(it->second.scope, it->second.storage);
    }
    
    compile_context().error_throw(location, "Jump target is not in scope");
  }
}


/**
 * Generate an exit path from this block to the specified target.
 * 
 * \param builder Where to jump to the new block from. This may
 * be modified by the function call to point to a new insertion point;
 * it should not be re-used without updating the insertion point anyway
 * since nothing should be inserted after a terminator instruction.
 * 
 * \return The scope which applies after the jump.
 */
void FunctionLowering::exit_to(Scope& from, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value) {
  // Locate storage and target scope first
  std::pair<FunctionLowering::Scope*, Tvm::ValuePtr<> > ei;
  if (target)
    ei = exit_info(from, target, location);
  
  // This will be modified as we pass through PHI nodes
  Tvm::ValuePtr<> phi_value = return_value;
  
  SourceLocation variable_location = location;
  for (Scope *scope = &from; ; scope = scope->parent()) {
    if (!scope->parent()) {
      if (!target) {
        // Rethrow exit... need to generate re-throw code
        PSI_NOT_IMPLEMENTED();
        return;
      } else if (target == m_return_target) {
        if (return_value)
          builder().return_(phi_value, location);
        else
          builder().return_void(location);
        return;
      } else {
        compile_context().error_throw(location, "Jump target is not in scope.");
      }
    } else {
      Scope::JumpMapType::const_iterator it = scope->m_jump_map.find(target);
      if (it != scope->m_jump_map.end()) {
        builder().br(it->second.block, variable_location);
        if (phi_value)
          Tvm::value_cast<Tvm::Phi>(it->second.storage)->add_edge(builder().block(), phi_value);
        return;
      } else if ((scope->m_variable.storage() == local_stack) && !is_primitive(scope->m_variable.type())) {
        // Only bother with scopes which have a variable constructed in

        // Branch to new block and destroy this variable
        Tvm::ValuePtr<Tvm::Block> next_block = m_output->new_block(scope->location(), scope->dominator());
        builder().br(next_block, variable_location);
        if (phi_value) {
          Tvm::ValuePtr<Tvm::Phi> new_phi = next_block->insert_phi(phi_value->type(), scope->location());
          new_phi->add_edge(builder().block(), phi_value);
          phi_value = new_phi;
        }

        builder().set_insert_point(next_block);
        destroy(scope->m_variable.type(), scope->m_variable.value(), variable_location);
        
        Scope::JumpData jd;
        jd.block = next_block;
        jd.scope = ei.first;
        jd.storage = ei.second;
        scope->m_jump_map.insert(std::make_pair(target, jd));

        // Destroy object
        variable_location = scope->m_statement->location();
      }
    }
  }
}

/**
 * \class FunctionLowering
 * 
 * Converts a Function to a Tvm::Function.
 * 
 * Variable lifecycles are tracked by "following scope" pointers. The scope which will
 * be current immediately after the current term is tracked, so that variables which
 * are about to go out of scope can be detected.
 */

/**
 * \brief Map a type into TVM.
 */
Tvm::ValuePtr<> FunctionLowering::run_type(const TreePtr<Term>& type) {
  if (TreePtr<StructType> struct_ty = dyn_treeptr_cast<StructType>(type)) {
    PSI_STD::vector<Tvm::ValuePtr<> > members;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii)
      members.push_back(run_type(*ii));
    return Tvm::FunctionalBuilder::struct_type(tvm_context(), members, type->location());
  } else if (TreePtr<UnionType> union_ty = dyn_treeptr_cast<UnionType>(type)) {
    PSI_STD::vector<Tvm::ValuePtr<> > members;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = struct_ty->members.begin(), ie = struct_ty->members.end(); ii != ie; ++ii)
      members.push_back(run_type(*ii));
    return Tvm::FunctionalBuilder::union_type(tvm_context(), members, type->location());
  } else if (TreePtr<PointerType> ptr_ty = dyn_treeptr_cast<PointerType>(type)) {
    return Tvm::FunctionalBuilder::pointer_type(run_type(ptr_ty->target_type), ptr_ty->location());
  } else {
    PSI_NOT_IMPLEMENTED();
  }
}

Tvm::ValuePtr<> FunctionLowering::run(Scope& scope, const TreePtr<Term>& term, const Variable& slot, Scope& following_scope) {
  if (TreePtr<Block> block = dyn_treeptr_cast<Block>(term)) {
    return run_block(scope, block, slot, following_scope, 0);
  } else if (TreePtr<IfThenElse> if_then_else = dyn_treeptr_cast<IfThenElse>(term)) {
    return run_if_then_else(scope, if_then_else, slot, following_scope);
  } else if (TreePtr<JumpGroup> jump_group = dyn_treeptr_cast<JumpGroup>(term)) {
    return run_jump_group(scope, jump_group, slot, following_scope);
  } else if (TreePtr<JumpTo> jump_to = dyn_treeptr_cast<JumpTo>(term)) {
    return run_jump(scope, jump_to, slot, following_scope);
  } else if (TreePtr<StatementRef> statement = dyn_treeptr_cast<StatementRef>(term)) {
    VariableMap::const_iterator ii = m_variables.find(statement->value);
    if (ii == m_variables.end())
      compile_context().error_throw(statement->location(), "Variable is not in scope");
    return variable_assign(scope, slot, *ii->second, following_scope, statement->location());
  } else {
    compile_context().error_throw(term->location(), "Function lowering failed: unknown term type", CompileError::error_internal);
  }
}

/**
 * Run the highest level of a context. In this function new local variables (in Blocks) may be created
 * in this context, rather than a child context being created to hold them.
 * 
 * \param following_scope Scope which will apply immediately after this call. This allows variables
 * which are about to go out of scope to be detected.
 */
Tvm::ValuePtr<> FunctionLowering::run_block(Scope& scope, const TreePtr<Block>& block, const Variable& slot, Scope& following_scope, unsigned index) {
  PSI_ASSERT(index <= block->statements.size());
  if (index == block->statements.size()) {
    return run(scope, block->value, slot, following_scope);
  } else {
    const TreePtr<Statement>& statement = block->statements[index];
    Variable var(scope, statement->value);
    var.assign(run(scope, statement->value, var, scope));
    Scope child_scope(scope, var, statement->location(), statement);
    return run_block(child_scope, block, slot, following_scope, index+1);
  }
}

/**
 * \brief Create TVM structure for If-Then-Else.
 */
Tvm::ValuePtr<> FunctionLowering::run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const Variable& slot, Scope& following_scope) {
  Variable condition(scope, if_then_else->condition);
  condition.assign(run(scope, if_then_else->condition, condition, scope));
  Tvm::ValuePtr<Tvm::Block> true_block = builder().new_block(if_then_else->true_value->location());
  Tvm::ValuePtr<Tvm::Block> false_block = builder().new_block(if_then_else->false_value->location());

  builder().cond_br(as_functional(condition, if_then_else->location()), true_block, false_block, if_then_else->location());
  Tvm::InstructionInsertPoint cond_insert = builder().insert_point();
  
  builder().set_insert_point(true_block);
  Tvm::ValuePtr<> true_value = run(scope, if_then_else->true_value, slot, following_scope);
  Tvm::InstructionInsertPoint true_insert = builder().insert_point();
  
  builder().set_insert_point(false_block);
  Tvm::ValuePtr<> false_value = run(scope, if_then_else->false_value, slot, following_scope);
  Tvm::InstructionInsertPoint false_insert = builder().insert_point();
  
  if (!is_bottom(if_then_else->true_value) && !is_bottom(if_then_else->false_value)) {
    builder().set_insert_point(cond_insert);
    Tvm::ValuePtr<Tvm::Block> exit_block = builder().new_block(if_then_else->location());
    Tvm::InstructionBuilder(true_insert).br(exit_block, if_then_else->location());
    Tvm::InstructionBuilder(false_insert).br(exit_block, if_then_else->location());
    builder().set_insert_point(exit_block);
    
    if (slot.storage() == local_functional) {
      Tvm::ValuePtr<Tvm::Phi> phi = builder().phi(true_value->type(), if_then_else->location());
      phi->add_edge(true_insert.block(), true_value);
      phi->add_edge(false_insert.block(), false_value);
      return phi;
    } else {
      return Tvm::ValuePtr<>();
    }
  } else if (!is_bottom(if_then_else->true_value)) {
    builder().set_insert_point(true_insert);
    return true_value;
  } else if (!is_bottom(if_then_else->false_value)) {
    builder().set_insert_point(false_insert);
    return false_value;
  } else {
    return Tvm::ValuePtr<>();
  }
}

/**
 * \brief Create TVM structure for a jump group.
 */
Tvm::ValuePtr<> FunctionLowering::run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const Variable& slot, Scope& following_scope) {
  std::vector<Tvm::ValuePtr<> > parameter_types;
  std::map<TreePtr<JumpTarget>, Tvm::ValuePtr<Tvm::Block> > initial_jump_map;
  for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    Tvm::ValuePtr<Tvm::Block> block = builder().new_block(ii->location());
    initial_jump_map.insert(std::make_pair(*ii, block));
    PSI_NOT_IMPLEMENTED(); // Need to check storage type of argument!
    if ((*ii)->argument)
      parameter_types.push_back(run_type((*ii)->argument->type));
  }
  
  // Construct a union of all jump target parameter types,
  // in order to allocate the least storage possible.
  Tvm::ValuePtr<> storage;
  if (!parameter_types.empty())
    storage = builder().alloca_(Tvm::FunctionalBuilder::union_type(tvm_context(), parameter_types, jump_group->location()), jump_group->location());

  Scope jump_scope(scope, initial_jump_map, storage, jump_group->location());

  std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, Tvm::ValuePtr<> > > non_bottom_values;
  
  Tvm::ValuePtr<> initial_value = run(jump_scope, jump_group->initial, slot, following_scope);
  Tvm::ValuePtr<Tvm::Block> initial_insert_block = builder().block();
  if (!is_bottom(jump_group->initial))
    non_bottom_values.push_back(std::make_pair(initial_insert_block, initial_value));

  for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    builder().set_insert_point(initial_insert_block);
    Tvm::ValuePtr<Tvm::Block> block = builder().new_block(ii->location());
    builder().set_insert_point(block);
    // Need to copy incoming value from ::storage to new location so that storage may be re-used for later jump.
    PSI_NOT_IMPLEMENTED();
    Tvm::ValuePtr<> value = run(jump_scope, (*ii)->value, slot, following_scope);
    if (!is_bottom((*ii)->value))
      non_bottom_values.push_back(std::make_pair(block, value));
  }
  
  if (non_bottom_values.size() > 1) {
    Tvm::ValuePtr<Tvm::Block> exit_block = builder().new_block(jump_group->location());
    for (std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, Tvm::ValuePtr<> > >::iterator ii = non_bottom_values.begin(), ie = non_bottom_values.end(); ii != ie; ++ii)
      Tvm::InstructionBuilder(ii->first).br(exit_block, jump_group->location());
    
    builder().set_insert_point(exit_block);
    if (slot.storage() == local_functional) {
      Tvm::ValuePtr<Tvm::Phi> phi = builder().phi(non_bottom_values.front().second->type(), jump_group->location());
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
Tvm::ValuePtr<> FunctionLowering::run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const Variable&, Scope&) {
  Tvm::ValuePtr<> return_value;
  if (jump_to->argument) {
    std::pair<Scope*, Tvm::ValuePtr<> > ei = exit_info(scope, jump_to->target, jump_to->location());
    Variable var(scope, jump_to->target, ei.second);
    return_value = run(scope, jump_to->argument, var, *ei.first);
  }

  exit_to(scope, jump_to->target, jump_to->location(), return_value);
  
  return Tvm::ValuePtr<>();
}
/**
 * \brief Assign the contents of one variable to another.
 * 
 * If dest is funtional, note that the result is not assigned to src but
 * passed via the return value, as in all other functions.
 */
Tvm::ValuePtr<> FunctionLowering::variable_assign(Scope& scope, const Variable& dest, const Variable& src, Scope& following_scope, const SourceLocation& location) {
  switch (src.storage()) {
  case local_functional:
    switch (dest.storage()) {
    case local_functional: return src.value();
    case local_stack: builder().store(src.value(), dest.value(), location); return Tvm::ValuePtr<>();
    default: compile_context().error_throw(location, "Local storage inference failed: cannot convert functional value to reference", CompileError::error_internal);
    }
    
  case local_lvalue_ref:
    switch (dest.storage()) {
    case local_functional: return as_functional(src, location);
    case local_lvalue_ref: return src.value();
    case local_rvalue_ref: compile_context().error_throw(location, "Local storage inference failed: cannot implicitly convert l-value to r-value", CompileError::error_internal);
    case local_stack: copy_construct(dest.type(), dest.value(), src.value(), location); return Tvm::ValuePtr<>();
    default: PSI_FAIL("unknown enum value");
    }
    
  case local_rvalue_ref:
    switch (dest.storage()) {
    case local_functional:
      return as_functional(src, location);

    case local_lvalue_ref:
    case local_rvalue_ref:
      return src.value();
    
    case local_stack:
      if (going_out_of_scope(scope, src, following_scope))
        move_construct(dest.type(), dest.value(), src.value(), location);
      else
        copy_construct(dest.type(), dest.value(), src.value(), location);
      return Tvm::ValuePtr<>();

    default: PSI_FAIL("unknown enum value");
    }
    
  case local_stack:
    switch (dest.storage()) {
    case local_functional:
      return as_functional(src, location);
    
    case local_lvalue_ref:
    case local_rvalue_ref:
      if (going_out_of_scope(scope, src, following_scope))
        compile_context().error(location, "Cannot return reference to variable going out of scope");
      return src.value();
    
    case local_stack:
      if (going_out_of_scope(scope, src, following_scope))
        move_construct(dest.type(), dest.value(), src.value(), location);
      else
        copy_construct(dest.type(), dest.value(), src.value(), location);
      return Tvm::ValuePtr<>();

    default: PSI_FAIL("unknown enum value");
    }

  default: PSI_FAIL("unknown enum value");
  }
}

/**
 * \brief Check whether a given variable goes out of scope between scope and following_scope.
 */
bool FunctionLowering::going_out_of_scope(Scope& scope, const Variable& var, Scope& following_scope) {
  for (Scope *sc = &scope; sc != &following_scope; sc = sc->parent()) {
    PSI_ASSERT_MSG(sc, "following_scope was not a parent of scope");
    if (&var == &sc->variable())
      return true;
  }

  // Check that variable exists somewhere above scope
  PSI_ASSERT_BLOCK(Scope *sc; for (sc = &following_scope; (sc && (&sc->variable() != &var)); sc = sc->parent());, !sc);

  return false;
}

/**
 * \brief Detect the type of local storage to be used by a term.
 */
LocalStorage FunctionLowering::storage(const TreePtr<Term>& term) {
  if (TreePtr<FunctionCall> call = dyn_treeptr_cast<FunctionCall>(term)) {
    TreePtr<FunctionType> target_type = treeptr_cast<FunctionType>(call->target->type);
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<Statement> stmt = dyn_treeptr_cast<Statement>(term)) {
    return (storage(stmt->value) == local_functional) ? local_functional : local_lvalue_ref;
  } else {
    compile_context().error_throw(term->location(), "Unknown tree in local storage detection", CompileError::error_internal);
  }
}

/**
 * \brief Allocate space on the stack for the result of evaluating a given term.
 */
Tvm::ValuePtr<> FunctionLowering::allocate_slot(const TreePtr<Term>& term) {
  if (storage(term) == local_stack) {
    return builder().alloca_(run_type(term->type), term->location());
  } else {
    return Tvm::ValuePtr<>();
  }
}

/**
 * \brief Convert argument to a functional value.
 * 
 * This should only be used on values whose types are primitive.
 */
Tvm::ValuePtr<> FunctionLowering::as_functional(const Variable& var, const SourceLocation& location) {
  PSI_ASSERT(is_primitive(var.type()));
  if (var.storage() == local_functional) {
    return var.value();
  } else {
    return builder().load(var.value(), location);
  }
}

/**
 * \brief Check whether this term has is of BottomType.
 * 
 * If the type of a term is bottom, then the term can never successfully
 * evaluate.
 */
bool FunctionLowering::is_bottom(const TreePtr<Term>& term) {
  return dyn_treeptr_cast<BottomType>(term->type);
}

/// \brief Detect whether a type is primitive so may live in registers rather than on the stack/heap.
bool FunctionLowering::is_primitive(const TreePtr<Term>& type) {
  PSI_NOT_IMPLEMENTED();
}
}
}
