#include "Compiler.hpp"
#include "Tree.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/InstructionBuilder.hpp"
#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Aggregate.hpp"

#include <boost/next_prior.hpp>

namespace Psi {
namespace Compiler {
class FunctionLowering {
  CompileContext *m_compile_context;

  Tvm::ValuePtr<Tvm::Function> m_output;
  TreePtr<JumpTarget> m_return_target;
  Tvm::ValuePtr<> m_return_storage;
  Tvm::InstructionBuilder m_builder;
  
  class Scope;

  std::pair<Scope*, Tvm::ValuePtr<> > exit_info(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location);
  void exit_to(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);
  
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
    local_rvalue_ref,
    /**
    * \brief Indicates this result cannot be evaluated.
    * 
    * This is not a storage class, but indicates that the result of the
    * expression being described can never be successfully evaluated.
    */
    local_bottom
  };

  /**
   * \brief Result of generating code to compute a variable.
   */
  class VariableResult {
    LocalStorage m_storage;
    Tvm::ValuePtr<> m_value;
    
    VariableResult(LocalStorage storage, const Tvm::ValuePtr<>& value) : m_storage(storage), m_value(value) {}

  public:
    VariableResult() : m_storage(local_bottom) {}
    
    static VariableResult bottom() {return VariableResult(local_bottom, Tvm::ValuePtr<>());}
    static VariableResult on_stack() {return VariableResult(local_stack, Tvm::ValuePtr<>());}
    static VariableResult in_register(LocalStorage storage, const Tvm::ValuePtr<>& value) {return VariableResult(storage, value);}

    /// \brief Whether this variable cannot successfully be computed
    LocalStorage storage() const {return m_storage;}
    /// \brief Value of this variable if it is not stored on the stack (i.e. functional or reference)
    const Tvm::ValuePtr<>& value() const {return m_value;}
  };
  
  /**
   * \brief Stores data about a named or unnamed local variable.
   * 
   * This object is created before the code to compute the variable is generated because
   * it is necessary to allocate output storage.
   */
  class Variable {
    friend class Scope;
    
    TreePtr<Term> m_type;
    Tvm::ValuePtr<> m_tvm_type;

    LocalStorage m_storage;
    Tvm::ValuePtr<> m_value;

  public:
    Variable() : m_storage(local_bottom) {}
    Variable(Scope& parent_scope, const TreePtr<Term>& value);
    Variable(Scope& parent_scope, const TreePtr<Term>& value, const Tvm::ValuePtr<>& tvm_value, LocalStorage storage);
    Variable(Scope& parent_scope, const TreePtr<JumpTarget>& jump_target, const Tvm::ValuePtr<>& stack);

    const TreePtr<Term>& type() const {return m_type;}
    const Tvm::ValuePtr<>& tvm_type() const {return m_tvm_type;}

    /// \brief Assign a value to this variable.
    void assign(const VariableResult& value);

    /**
     * \brief Get the value of this variable.
     * 
     * Interpretation of this depends on storage, because it may be a pointer to tvm_type()
     * rather than just a value of that type.
     */
    const Tvm::ValuePtr<>& value() const {PSI_ASSERT(m_storage != local_bottom); return m_value;}
    LocalStorage storage() const {return m_storage;}

    /// \brief Get the stack slot for this variable.
    const Tvm::ValuePtr<>& stack_slot() const {PSI_ASSERT(m_storage == local_bottom); return m_value;}
  };

  typedef boost::unordered_map<TreePtr<>, const Variable*> VariableMap;
  VariableMap m_variables;

  struct JumpData {
    Tvm::ValuePtr<Tvm::Block> block;
    Scope *scope;
    
    /**
      * Holds either a stack pointer if the jump target parameter is stored on the stack,
      * or a reference to a PHI node.
      */
    Tvm::ValuePtr<> storage;
  };
  
  typedef std::map<TreePtr<JumpTarget>, JumpData> JumpMapType;
  
  class Scope {
    friend std::pair<Scope*, Tvm::ValuePtr<> > FunctionLowering::exit_info(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location);
    friend void FunctionLowering::exit_to(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);
    
    Scope *m_parent;
    Tvm::ValuePtr<Tvm::Block> m_dominator;
    FunctionLowering *m_shared;
    
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
    TreePtr<> m_variable_key;

  public:
    Scope(FunctionLowering *shared, const SourceLocation& location);
    Scope(Scope& parent, const Variable& variable, const SourceLocation& location, const TreePtr<>& key=TreePtr<>());
    Scope(Scope& parent, JumpMapType& initial_jump_map, const SourceLocation& location);
    ~Scope();
    
    CompileContext& compile_context() {return m_shared->compile_context();}

    FunctionLowering& shared() const {return *m_shared;}
    Scope *parent() {return m_parent;}
    const Tvm::ValuePtr<Tvm::Block>& dominator() const {return m_dominator;}
    const SourceLocation& location() const {return m_location;}
    const Variable& variable() const {return m_variable;}
    const JumpMapType& jump_map() const {return m_jump_map;}
  };

  CompileContext& compile_context() {return *m_compile_context;}
  Tvm::Context& tvm_context();

  bool is_primitive(const TreePtr<Term>& type);
  VariableResult variable_assign(Scope& scope, const Variable& dest, const Variable& src, Scope& following_scope, const SourceLocation& location);
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
  
  Tvm::ValuePtr<> as_functional(const Variable& var, const SourceLocation& location);
  
  VariableResult run(Scope& scope, const TreePtr<Term>& term, const Variable& slot, Scope& following_scope);
  VariableResult run_block(Scope& scope, const TreePtr<Block>& block, const Variable& slot, Scope& following_scope, unsigned index);
  VariableResult run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const Variable& slot, Scope& following_scope);
  VariableResult run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const Variable& slot, Scope& following_scope);
  VariableResult run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const Variable& slot, Scope& following_scope);
  
  typedef std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, VariableResult> > MergeExitList;
  static bool merge_exit_list_entry_bottom(const MergeExitList::value_type& el);
  static LocalStorage merge_storage(LocalStorage x, LocalStorage y);
  VariableResult merge_exit(Scope& scope, const Variable& slot, MergeExitList& variables); 
  
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
m_tvm_type(parent_scope.shared().run_type(m_type)),
m_storage(local_bottom) {
  m_value = parent_scope.shared().builder().alloca_(m_tvm_type, value->location());
}

FunctionLowering::Variable::Variable(Scope&, const TreePtr<Term>& value, const Tvm::ValuePtr<>& tvm_value, LocalStorage storage)
: m_type(value->type),
m_tvm_type(tvm_value->type()),
m_storage(storage),
m_value(tvm_value) {
}

#if 0
FunctionLowering::Variable::Variable(Scope& parent_scope, const TreePtr<JumpTarget>& jump_target, const Tvm::ValuePtr<>& stack)
: m_type(jump_target->argument->type),
m_tvm_type(parent_scope.shared().run_type(m_type)) {
  switch (jump_target->argument_mode) {
  case result_mode_by_value: m_storage = local_stack; break;
  case result_mode_lvalue: m_storage = local_lvalue_ref; break;
  case result_mode_rvalue: m_storage = local_rvalue_ref; break;
  default: PSI_FAIL("unknown enum value");
  }

  if (m_storage == local_stack)
    m_value = Tvm::FunctionalBuilder::union_element_ptr(stack, parent_scope.shared().run_type(jump_target->argument->type), jump_target->location());
}
#endif

void FunctionLowering::Variable::assign(const VariableResult& value) {
  // storage should be bottom until this variable is assigned.
  PSI_ASSERT(m_storage == local_bottom);
  if (value.storage() != local_stack) {
    // Since this value is not on the stack, remove the stack allocation instruction.
    Tvm::value_cast<Tvm::Instruction>(m_value)->remove();
    m_storage = value.storage();
    m_value = value.value();
  }
}

FunctionLowering::Scope::Scope(FunctionLowering *shared, const SourceLocation& location)
: m_parent(NULL),
m_shared(shared),
m_location(location) {
}

FunctionLowering::Scope::Scope(Scope& parent, const Variable& variable, const SourceLocation& location, const TreePtr<>& key)
: m_parent(&parent),
m_dominator(parent.m_shared->builder().block()),
m_shared(parent.m_shared),
m_variable(variable),
m_location(location),
m_variable_key(key) {
  if (key) {
    if (!m_shared->m_variables.insert(std::make_pair(key, &m_variable)).second)
      compile_context().error(key->location(), "Statemen appears in more than one block.");
  }
}

/**
 * \brief Create a new scope with a jump map.
 * 
 * \param initial_jump_map Initial jump map (more entries are generated
 * as required). This object is cleared by this function.
 */
FunctionLowering::Scope::Scope(Scope& parent, JumpMapType& initial_jump_map, const SourceLocation& location)
: m_parent(&parent),
m_shared(parent.m_shared),
m_location(location) {
  m_jump_map.swap(initial_jump_map);
  for (JumpMapType::iterator ii = initial_jump_map.begin(), ie = initial_jump_map.end(); ii != ie; ++ii)
    ii->second.scope = this;
}

FunctionLowering::Scope::~Scope() {
  if (m_variable_key) {
    VariableMap::iterator it = m_shared->m_variables.find(m_variable_key);
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
      JumpMapType::const_iterator it = scope->m_jump_map.find(target);
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
      JumpMapType::const_iterator it = scope->m_jump_map.find(target);
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
        Tvm::ValuePtr<Tvm::Phi> next_phi;
        if (phi_value) {
          next_phi = next_block->insert_phi(phi_value->type(), scope->location());
          next_phi->add_edge(builder().block(), phi_value);
          phi_value = next_phi;
        }

        builder().set_insert_point(next_block);
        destroy(scope->m_variable.type(), scope->m_variable.value(), variable_location);
        
        JumpData jd;
        jd.block = next_block;
        jd.scope = ei.first;
        jd.storage = next_phi ? Tvm::ValuePtr<>(next_phi) : ei.second;
        scope->m_jump_map.insert(std::make_pair(target, jd));

        // Destroy object
        variable_location = scope->location();
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

FunctionLowering::VariableResult FunctionLowering::run(Scope& scope, const TreePtr<Term>& term, const Variable& slot, Scope& following_scope) {
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
    PSI_NOT_IMPLEMENTED();
  }
}

/**
 * Run the highest level of a context. In this function new local variables (in Blocks) may be created
 * in this context, rather than a child context being created to hold them.
 * 
 * \param following_scope Scope which will apply immediately after this call. This allows variables
 * which are about to go out of scope to be detected.
 */
FunctionLowering::VariableResult FunctionLowering::run_block(Scope& scope, const TreePtr<Block>& block, const Variable& slot, Scope& following_scope, unsigned index) {
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
FunctionLowering::VariableResult FunctionLowering::run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const Variable& slot, Scope& following_scope) {
  Variable condition(scope, if_then_else->condition);
  condition.assign(run(scope, if_then_else->condition, condition, scope));
  Tvm::ValuePtr<Tvm::Block> true_block = builder().new_block(if_then_else->true_value->location());
  Tvm::ValuePtr<Tvm::Block> false_block = builder().new_block(if_then_else->false_value->location());

  builder().cond_br(as_functional(condition, if_then_else->location()), true_block, false_block, if_then_else->location());
  Tvm::InstructionInsertPoint cond_insert = builder().insert_point();
  
  MergeExitList results;
  
  builder().set_insert_point(true_block);
  VariableResult true_value = run(scope, if_then_else->true_value, slot, following_scope);
  results.push_back(std::make_pair(builder().block(), true_value));
  
  builder().set_insert_point(false_block);
  VariableResult false_value = run(scope, if_then_else->false_value, slot, following_scope);
  results.push_back(std::make_pair(builder().block(), false_value));
  
  return merge_exit(scope, slot, results);
}

/**
 * \brief Create TVM structure for a jump group.
 */
FunctionLowering::VariableResult FunctionLowering::run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const Variable& slot, Scope& following_scope) {
  JumpMapType initial_jump_map;
  std::vector<Tvm::ValuePtr<> > parameter_types;
  for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    JumpData jd;
    jd.block = builder().new_block(ii->location());
    if ((*ii)->argument) {
      Tvm::ValuePtr<> type = run_type((*ii)->argument->type);
      if ((*ii)->argument_mode == result_mode_by_value)
        parameter_types.push_back(type);
      else
        jd.storage = jd.block->insert_phi(type, (*ii)->location());
    }
    initial_jump_map.insert(std::make_pair(*ii, jd));
  }
  
  // Construct a union of all jump target parameter types,
  // in order to allocate the least storage possible.
  if (!parameter_types.empty()) {
    Tvm::ValuePtr<> storage_type = Tvm::FunctionalBuilder::union_type(tvm_context(), parameter_types, jump_group->location());
    Tvm::ValuePtr<> storage = builder().alloca_(storage_type, jump_group->location());
    for (JumpMapType::iterator ii = initial_jump_map.begin(), ie = initial_jump_map.end(); ii != ie; ++ii) {
      if (ii->first->argument && (ii->first->argument_mode == result_mode_by_value)) {
        PSI_ASSERT(!ii->second.storage);
        ii->second.storage = Tvm::FunctionalBuilder::union_element_ptr(storage, run_type(ii->first->argument->type), ii->first->location());
      }
    }
  }

  Scope jump_scope(scope, initial_jump_map, jump_group->location());

  MergeExitList results;
  
  VariableResult initial_value = run(jump_scope, jump_group->initial, slot, following_scope);
  Tvm::ValuePtr<Tvm::Block> group_dominator = builder().block();
  results.push_back(std::make_pair(builder().block(), initial_value));

  for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    const JumpData& jd = jump_scope.jump_map().find(*ii)->second;
    builder().set_insert_point(m_output->new_block((*ii)->location(), group_dominator));

    VariableResult entry_result;
    if ((*ii)->argument) {
      Variable entry_variable(jump_scope, (*ii)->argument);
      VariableResult entry_variable_result;
      switch ((*ii)->argument_mode) {
      case result_mode_by_value: {
        move_construct_destroy((*ii)->argument->type, entry_variable.stack_slot(), jd.storage, (*ii)->location());
        entry_variable_result = VariableResult::on_stack();
        break;
      }
      
      case result_mode_lvalue:
        PSI_ASSERT(Tvm::isa<Tvm::Phi>(jd.storage));
        entry_variable_result = VariableResult::in_register(local_lvalue_ref, jd.storage);
        break;

      case result_mode_rvalue:
        PSI_ASSERT(Tvm::isa<Tvm::Phi>(jd.storage));
        entry_variable_result = VariableResult::in_register(local_rvalue_ref, jd.storage);
        break;

      default: PSI_FAIL("unknown enum value");
      }
      entry_variable.assign(entry_variable_result);

      Scope entry_scope(jump_scope, entry_variable, (*ii)->location(), (*ii)->argument);
      entry_result = run(entry_scope, (*ii)->value, slot, following_scope);
    } else {
      entry_result = run(jump_scope, (*ii)->value, slot, following_scope);
    }
    
    results.push_back(std::make_pair(builder().block(), entry_result));
  }
  
  return merge_exit(jump_scope, slot, results);
}

/**
 * Handle a jump.
 */
FunctionLowering::VariableResult FunctionLowering::run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const Variable&, Scope&) {
  Tvm::ValuePtr<> result_value;
  if (jump_to->argument) {
    std::pair<Scope*, Tvm::ValuePtr<> > ei = exit_info(scope, jump_to->target, jump_to->location());
    Variable var(scope, jump_to->target, ei.second);
    var.assign(run(scope, jump_to->argument, var, *ei.first));
    result_value = var.value();
  }

  exit_to(scope, jump_to->target, jump_to->location(), result_value);
  
  return VariableResult::bottom();
}

bool FunctionLowering::merge_exit_list_entry_bottom(const MergeExitList::value_type& el) {
  return el.second.storage() == local_bottom;
}

FunctionLowering::LocalStorage FunctionLowering::merge_storage(LocalStorage x, LocalStorage y) {
  PSI_ASSERT(x != local_bottom);
  PSI_ASSERT(y != local_bottom);

  switch (x) {
  case local_stack:
    return local_stack;
    
  case local_lvalue_ref:
    switch (y) {
    case local_lvalue_ref:
    case local_rvalue_ref: return local_lvalue_ref;
    case local_stack:
    case local_functional: return local_stack;
    default: PSI_FAIL("unknown enum value");
    }
    
  case local_rvalue_ref:
    switch (y) {
    case local_lvalue_ref: return local_lvalue_ref;
    case local_rvalue_ref: return local_rvalue_ref;
    case local_stack:
    case local_functional: return local_stack;
    default: PSI_FAIL("unknown enum value");
    }
  
  case local_functional:
    return (y == local_functional) ? local_functional : local_stack;
    
  default: PSI_FAIL("unknown enum value");  
  }
}

/**
 * \brief Merge different execution contexts into a single context.
 * 
 * This is used for If-Then-Else and jump groups.
 * 
 * \param values List of exit blocks and values from each block to merge into a single execution path.
 * This is modified by this function. Should really be an r-value ref in C++11.
 */
FunctionLowering::VariableResult FunctionLowering::merge_exit(Scope& scope, const Variable& slot, MergeExitList& values) {
  // Erase all bottom values
  values.erase(std::remove_if(values.begin(), values.end(), &FunctionLowering::merge_exit_list_entry_bottom), values.end());
  
  if (values.size() > 1) {
    Tvm::ValuePtr<Tvm::Block> exit_block = m_output->new_block(scope.location(), scope.dominator());
    LocalStorage final_storage = values.front().second.storage();
    for (MergeExitList::const_iterator ii = boost::next(values.begin()), ie = values.end(); ii != ie; ++ii) {
      final_storage = merge_storage(final_storage, ii->second.storage());
      Tvm::InstructionBuilder(ii->first).br(exit_block, scope.location());
    }
    
    builder().set_insert_point(exit_block);

    if (final_storage == local_stack) {
      for (MergeExitList::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii) {
        switch (ii->second.storage()) {
        case local_stack: break;
        case local_lvalue_ref: copy_construct(slot.type(), slot.stack_slot(), ii->second.value(), scope.location()); break;
        case local_rvalue_ref: move_construct(slot.type(), slot.stack_slot(), ii->second.value(), scope.location()); break;
        case local_functional: builder().store(ii->second.value(), slot.stack_slot(), scope.location()); break;
        default: PSI_FAIL("unknown enum value");
        }
      }

      return VariableResult::on_stack();
    } else {
      Tvm::ValuePtr<> phi_type = (final_storage == local_functional) ? slot.tvm_type() : Tvm::FunctionalBuilder::pointer_type(slot.tvm_type(), scope.location());
      Tvm::ValuePtr<Tvm::Phi> merged_value = builder().phi(phi_type, scope.location());
      for (MergeExitList::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
        merged_value->add_edge(ii->first, ii->second.value());
      
      return VariableResult::in_register(final_storage, merged_value);
    }
  } else if (values.size() == 1) {
    builder().set_insert_point(values.front().first);
    return values.front().second;
  } else {
    return VariableResult::bottom();
  }
}

/**
 * \brief Assign the contents of one variable to another.
 * 
 * If dest is funtional, note that the result is not assigned to src but
 * passed via the return value, as in all other functions.
 */
FunctionLowering::VariableResult FunctionLowering::variable_assign(Scope& scope, const Variable& dest, const Variable& src, Scope& following_scope, const SourceLocation& location) {
  switch (src.storage()) {
  case local_functional:
    switch (dest.storage()) {
    case local_functional: return VariableResult::in_register(local_functional, src.value());
    case local_stack: builder().store(src.value(), dest.value(), location); return VariableResult::on_stack();
    default: compile_context().error_throw(location, "Local storage inference failed: cannot convert functional value to reference", CompileError::error_internal);
    }
    
  case local_lvalue_ref:
    switch (dest.storage()) {
    case local_functional: return VariableResult::in_register(local_functional, as_functional(src, location));
    case local_lvalue_ref: return VariableResult::in_register(local_lvalue_ref, src.value());
    case local_rvalue_ref: compile_context().error_throw(location, "Local storage inference failed: cannot implicitly convert l-value to r-value", CompileError::error_internal);
    case local_stack: copy_construct(dest.type(), dest.value(), src.value(), location); return VariableResult::on_stack();
    default: PSI_FAIL("unknown enum value");
    }
    
  case local_rvalue_ref:
    switch (dest.storage()) {
    case local_functional: return VariableResult::in_register(local_functional, as_functional(src, location));
    case local_lvalue_ref:
    case local_rvalue_ref: return VariableResult::in_register(dest.storage(), src.value());
    
    case local_stack:
      if (going_out_of_scope(scope, src, following_scope))
        move_construct(dest.type(), dest.value(), src.value(), location);
      else
        copy_construct(dest.type(), dest.value(), src.value(), location);
      return VariableResult::on_stack();

    default: PSI_FAIL("unknown enum value");
    }
    
  case local_stack:
    switch (dest.storage()) {
    case local_functional:
      return VariableResult::in_register(local_functional, as_functional(src, location));
    
    case local_lvalue_ref:
    case local_rvalue_ref:
      if (going_out_of_scope(scope, src, following_scope))
        compile_context().error(location, "Cannot return reference to variable going out of scope");
      return VariableResult::in_register(dest.storage(), src.value());
    
    case local_stack:
      if (going_out_of_scope(scope, src, following_scope))
        move_construct(dest.type(), dest.value(), src.value(), location);
      else
        copy_construct(dest.type(), dest.value(), src.value(), location);
      return VariableResult::on_stack();

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

/// \brief Detect whether a type is primitive so may live in registers rather than on the stack/heap.
bool FunctionLowering::is_primitive(const TreePtr<Term>& type) {
  PSI_NOT_IMPLEMENTED();
}
}
}
