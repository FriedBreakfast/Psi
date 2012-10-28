#include "Compiler.hpp"
#include "Tree.hpp"
#include "Interface.hpp"
#include "TypeMapping.hpp"
#include "TvmLowering.hpp"
#include "SharedMap.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/InstructionBuilder.hpp"
#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Aggregate.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/next_prior.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

namespace Psi {
namespace Compiler {
/**
 * \class TvmFunctionLowering
 * 
 * Converts a Function to a Tvm::Function.
 * 
 * Variable lifecycles are tracked by "following scope" pointers. The scope which will
 * be current immediately after the current term is tracked, so that variables which
 * are about to go out of scope can be detected.
 */
class TvmFunctionLowering {
  TvmCompiler *m_tvm_compiler;

  TreePtr<Module> m_module;
  Tvm::ValuePtr<Tvm::Function> m_output;
  TreePtr<JumpTarget> m_return_target;
  Tvm::ValuePtr<> m_return_storage;
  Tvm::InstructionBuilder m_builder;
  
  class Scope;
  class FunctionalBuilderCallback;
  class TryFinallyCleanup;

  std::pair<Scope*, Tvm::ValuePtr<> > exit_info(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location);
  void exit_to(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);
  
  /**
   * \brief Holds variable storage before the storage type of a variable is known.
   */
  class VariableSlot {
    Tvm::ValuePtr<> m_slot;
    
  public:
    VariableSlot(Scope& parent_scope, const TreePtr<Term>& value, const Tvm::ValuePtr<>& stack_slot=Tvm::ValuePtr<>());
    
    const Tvm::ValuePtr<>& slot() const {return m_slot;}
    void destroy_slot() {Tvm::value_cast<Tvm::Instruction>(m_slot)->remove();}
    void clear() {m_slot.reset();}
  };

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
  typedef SharedMap<TreePtr<>, TvmResult> VariableMapType;
  
  class CleanupCallback {
  public:
    virtual void run(Scope& scope) = 0;
  };
  
  class Scope : boost::noncopyable {
    friend std::pair<Scope*, Tvm::ValuePtr<> > TvmFunctionLowering::exit_info(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location);
    friend void TvmFunctionLowering::exit_to(Scope& scope, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);
    
    Scope *m_parent;
    /// Location of this scope
    SourceLocation m_location;
    
    Tvm::ValuePtr<Tvm::Block> m_dominator;
    TvmFunctionLowering *m_shared;
    
    /**
     * \brief Jumps out of this context which have already been built, plus jumps into immediate child scopes.
     * 
     * Note that a NULL JumpTarget represents an exception 
     */
    JumpMapType m_jump_map;
    VariableMapType m_variables;
    TvmResult m_variable;
    CleanupCallback *m_cleanup;
    
    void init(Scope& parent);

  public:
    Scope(TvmFunctionLowering *shared, const SourceLocation& location);
    Scope(Scope& parent, const SourceLocation& location, const TvmResult& result, VariableSlot& slot, const TreePtr<>& key=TreePtr<>());
    Scope(Scope& parent, const SourceLocation& location, CleanupCallback *cleanup);
    Scope(Scope& parent, const SourceLocation& location, const JumpMapType& initial_jump_map);
    
    CompileContext& compile_context() {return m_shared->compile_context();}

    TvmFunctionLowering& shared() const {return *m_shared;}
    Scope *parent() {return m_parent;}
    const Tvm::ValuePtr<Tvm::Block>& dominator() const {return m_dominator;}
    const SourceLocation& location() const {return m_location;}
    const JumpMapType& jump_map() const {return m_jump_map;}
    const VariableMapType& variables() const {return m_variables;}
    TvmResult variable() const {return m_variable;}
    
    bool has_cleanup() const {return m_cleanup || (m_variable.storage() == tvm_storage_stack);}
    void cleanup();
  };
  
  class ScopeList {
    boost::ptr_vector<Scope> m_list;
    Scope *m_current;
    
  public:
    ScopeList(Scope& parent) : m_current(&parent) {}
    void push(Scope *scope) {m_list.push_back(scope); m_current = scope;}
    void pop() {m_current = m_list.back().parent(); m_list.pop_back();}
    Scope& current() {return *m_current;}
    void cleanup();
  };

  TvmCompiler& tvm_compiler() {return *m_tvm_compiler;}
  CompileContext& compile_context() {return m_tvm_compiler->compile_context();}
  Tvm::Context& tvm_context() {return m_output->context();}

  bool going_out_of_scope(Scope& scope, const Tvm::ValuePtr<>& var, Scope& following_scope);
  void destroy_variable(Scope& scope);
  
  Tvm::ValuePtr<> move_constructible_interface(const TreePtr<Term>& type);
  Tvm::ValuePtr<> copy_constructible_interface(const TreePtr<Term>& type);
  bool is_primitive(Scope& scope, const TreePtr<Term>& type);
  void default_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const SourceLocation& location);
  void copy_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void move_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void move_construct_destroy(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void destroy(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& ptr, const SourceLocation& location);
  
  TvmResult run(Scope& scope, const TreePtr<Term>& term, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_block(Scope& scope, const TreePtr<Block>& block, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_try_finally(Scope& scope, const TreePtr<TryFinally>& try_finally, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_call(Scope& scope, const TreePtr<FunctionCall>& call, const VariableSlot& slot, Scope& following_scope);
  
  Tvm::ValuePtr<> run_functional(Scope& scope, const TreePtr<Term>& term);
  
  typedef std::vector<std::pair<Tvm::ValuePtr<Tvm::Block>, TvmResult> > MergeExitList;
  static bool merge_exit_list_entry_bottom(const MergeExitList::value_type& el);
  static TvmStorage merge_storage(TvmStorage x, TvmStorage y);
  TvmResult merge_exit(Scope& scope, const TreePtr<Term>& type, const VariableSlot& slot, MergeExitList& variables); 
  
  TvmResult run_type(Scope& scope, const TreePtr<Term>& type);

  /**
   * \brief Get the instruction builder for this function.
   * 
   * Note that the insertion pointer of this builder is modified throughout
   * the lowering process, and its state must be maintained carefully.
   */
  Tvm::InstructionBuilder& builder() {return m_builder;}
  
public:
  void run_body(TvmCompiler *tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output);
};

TvmFunctionLowering::VariableSlot::VariableSlot(Scope& parent_scope, const TreePtr<Term>& value, const Tvm::ValuePtr<>& stack_slot) {
  if (stack_slot) {
    m_slot = stack_slot;
  } else if (!tree_isa<FunctionType>(value->type) && !tree_isa<BottomType>(value->type)) {
    TvmResult tvm_type = parent_scope.shared().run_type(parent_scope, value->type);
    PSI_ASSERT(tvm_type.storage() == tvm_storage_functional);
    m_slot = parent_scope.shared().builder().alloca_(tvm_type.value(), value->location());
  }
}

TvmFunctionLowering::Scope::Scope(TvmFunctionLowering *shared, const SourceLocation& location)
: m_parent(NULL),
m_location(location),
m_shared(shared),
m_cleanup(NULL) {
}

void TvmFunctionLowering::Scope::init(Scope& parent) {
  m_cleanup = NULL;
  m_parent = &parent;
  m_dominator = parent.m_shared->builder().block();
  m_shared = parent.m_shared;
  m_variables = parent.m_variables;
}

TvmFunctionLowering::Scope::Scope(Scope& parent, const SourceLocation& location, const TvmResult& result, VariableSlot& slot, const TreePtr<>& key)
: m_location(location) {
  init(parent);
  
  if (key && !m_variables.insert(std::make_pair(key, result)))
    m_shared->compile_context().error_throw(location, "Overlapping variable definitions");

  m_variable = result;
  if (result.storage() == tvm_storage_stack) {
    PSI_ASSERT(result.value() == slot.slot());
  } else {
    slot.destroy_slot();
  }

  slot.clear();
}

TvmFunctionLowering::Scope::Scope(Scope& parent, const SourceLocation& location, CleanupCallback *cleanup)
: m_location(location) {
  init(parent);
  m_cleanup = cleanup;
}

TvmFunctionLowering::Scope::Scope(Scope& parent, const SourceLocation& location, const JumpMapType& initial_jump_map)
: m_location(location) {
  init(parent);
  m_jump_map = initial_jump_map;
}

/**
 * \brief Generate cleanup code for this scope.
 */
void TvmFunctionLowering::Scope::cleanup() {
  if (m_cleanup) {
    PSI_ASSERT(m_variable.storage() == tvm_storage_bottom);
    m_cleanup->run(*this);
  } else if (m_variable.storage() == tvm_storage_stack) {
    shared().destroy(*this, m_variable.type(), m_variable.value(), m_location);
  }
}

/**
 * \brief Clean up all variables in a scope list.
 */
void TvmFunctionLowering::ScopeList::cleanup() {
  for (boost::ptr_vector<Scope>::reverse_iterator ii = m_list.rbegin(), ie = m_list.rend(); ii != ie; ++ii)
    ii->cleanup();
}

void TvmFunctionLowering::run_body(TvmCompiler *tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output) {
  m_tvm_compiler = tvm_compiler;
  m_output = output;
  m_module = function->module;
  
  TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(function->type);
  
  ResultMode exit_result_mode = ftype->result_mode;
  switch (ftype->result_mode) {
  case result_mode_by_value:
    if (output->function_type()->sret()) {
      m_return_storage = output->parameters().back();
      exit_result_mode = result_mode_by_value;
    } else {
      exit_result_mode = result_mode_functional;
    }
    break;
    
  case result_mode_functional:
  case result_mode_lvalue:
  case result_mode_rvalue:
    break;

  default: PSI_FAIL("Unknown function result mode");
  }

  // We need this to be non-NULL
  m_return_target = function->return_target;
  if (!m_return_target) {
    TreePtr<Anonymous> return_argument(new Anonymous(ftype->result_type, function.location()));
    m_return_target.reset(new JumpTarget(compile_context(), TreePtr<Term>(), exit_result_mode, return_argument, function.location()));
  }
  
  Tvm::ValuePtr<Tvm::Block> entry_block = output->new_block(function.location());
  m_builder.set_insert_point(entry_block);

  Scope outer_scope(this, function->location());
  TreePtr<JumpTo> exit_jump(new JumpTo(m_return_target, function->body, function.location()));
  VariableSlot dummy_var(outer_scope, exit_jump);
  run_jump(outer_scope, exit_jump, dummy_var, outer_scope);
}

/**
 * \brief Lower a function.
 * 
 * Constructs a TvmFunctionLowering object and runs it.
 */
void tvm_lower_function(TvmCompiler& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output) {
  TvmFunctionLowering().run_body(&tvm_compiler, function, output);
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
std::pair<TvmFunctionLowering::Scope*, Tvm::ValuePtr<> > TvmFunctionLowering::exit_info(Scope& from, const TreePtr<JumpTarget>& target, const SourceLocation& location) {
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
 */
void TvmFunctionLowering::exit_to(Scope& from, const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value) {
  // Locate storage and target scope first
  std::pair<TvmFunctionLowering::Scope*, Tvm::ValuePtr<> > ei;
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
      } else if (scope->has_cleanup()) {
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
        scope->cleanup();
        
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

class TvmFunctionLowering::FunctionalBuilderCallback : public TvmFunctionalBuilderCallback {
  Scope *m_scope;
  
public:
  FunctionalBuilderCallback(Scope *scope) : m_scope(scope) {}
  
  virtual TvmResult build_hook(const TreePtr<Term>& term) {
    if (TreePtr<StatementRef> st = dyn_treeptr_cast<StatementRef>(term)) {
      const TvmResult *var = m_scope->variables().lookup(st->value);
      if (!var)
        m_scope->shared().compile_context().error_throw(st->location(), "Variable is not in scope");
      if (var->storage() != tvm_storage_functional)
        m_scope->shared().compile_context().error_throw(st->location(), "Cannot use non-constant variable as part of type");
      return *var;
    } else if (TreePtr<Global> gl = dyn_treeptr_cast<Global>(term)) {
      return TvmResult::in_register(gl->type, tvm_storage_lvalue_ref, m_scope->shared().tvm_compiler().build_global_in(gl, m_scope->shared().m_module));
    } else if (TreePtr<Constant> cns = dyn_treeptr_cast<Constant>(term)) {
      return m_scope->shared().tvm_compiler().build(cns);
    } else {
      PSI_FAIL(si_vptr(term.get())->classname);
    }
  }
  
  virtual TvmResult build_define_hook(const TreePtr<GlobalDefine>& define) {
    return m_scope->shared().tvm_compiler().build(define);
  }
  
  virtual TvmGenericResult build_generic_hook(const TreePtr<GenericType>& generic) {
    return m_scope->shared().tvm_compiler().build_generic(generic);
  }
  
  virtual Tvm::ValuePtr<> load_hook(const Tvm::ValuePtr<>& ptr, const SourceLocation& location) {
    return m_scope->shared().builder().load(ptr, location);
  }
};

/**
 * \brief Map a type into TVM.
 */
TvmResult TvmFunctionLowering::run_type(Scope& scope, const TreePtr<Term>& type) {
  FunctionalBuilderCallback cb(&scope);
  TvmFunctionalBuilder builder(&compile_context(), &tvm_context(), &cb);
  return builder.build_type(type);
}

Tvm::ValuePtr<> TvmFunctionLowering::run_functional(Scope& scope, const TreePtr<Term>& term) {
  VariableSlot slot(scope, term->type);
  TvmResult result = run(scope, term, slot, scope);
  Scope var_scope(scope, term->location(), result, slot);

  Tvm::ValuePtr<> tvm_result;
  switch (result.storage()) {
  case tvm_storage_functional:
    tvm_result = result.value();
    break;
    
  case tvm_storage_stack:
  case tvm_storage_lvalue_ref:
  case tvm_storage_rvalue_ref:
    tvm_result = m_builder.load(result.value(), term->location());
    break;
    
  case tvm_storage_bottom:
    tvm_result = Tvm::FunctionalBuilder::undef(Tvm::value_cast<Tvm::PointerType>(slot.slot()->type())->target_type(), term->location());
    break;
    
  default: PSI_FAIL("Unrecognised enum value");
  }
  
  var_scope.cleanup();
  
  return tvm_result;
}

TvmResult TvmFunctionLowering::run(Scope& scope, const TreePtr<Term>& term, const VariableSlot& slot, Scope& following_scope) {
  if (TreePtr<Global> global = dyn_treeptr_cast<Global>(term)) {
    return TvmResult::in_register(global->type, tvm_storage_lvalue_ref, tvm_compiler().build_global_in(global, m_module));
  } else if (TreePtr<Block> block = dyn_treeptr_cast<Block>(term)) {
    return run_block(scope, block, slot, following_scope);
  } else if (TreePtr<IfThenElse> if_then_else = dyn_treeptr_cast<IfThenElse>(term)) {
    return run_if_then_else(scope, if_then_else, slot, following_scope);
  } else if (TreePtr<JumpGroup> jump_group = dyn_treeptr_cast<JumpGroup>(term)) {
    return run_jump_group(scope, jump_group, slot, following_scope);
  } else if (TreePtr<JumpTo> jump_to = dyn_treeptr_cast<JumpTo>(term)) {
    return run_jump(scope, jump_to, slot, following_scope);
  } else if (TreePtr<TryFinally> try_finally = dyn_treeptr_cast<TryFinally>(term)) {
    return run_try_finally(scope, try_finally, slot, following_scope);
  } else if (TreePtr<StatementRef> statement = dyn_treeptr_cast<StatementRef>(term)) {
    const TvmResult *var = scope.variables().lookup(statement->value);
    if (!var)
      compile_context().error_throw(statement->location(), "Variable is not in scope");
    return (var->storage() == tvm_storage_stack) ?
      TvmResult::in_register(var->type(), tvm_storage_lvalue_ref, var->value()) : *var;
  } else if (TreePtr<FunctionCall> call = dyn_treeptr_cast<FunctionCall>(term)) {
    return run_call(scope, call, slot, following_scope);
  } else {
    FunctionalBuilderCallback callback(&scope);
    TvmFunctionalBuilder builder(&compile_context(), &tvm_context(), &callback);
    return builder.build(term);
  }
}

/**
 * Run the highest level of a context. In this function new local variables (in Blocks) may be created
 * in this context, rather than a child context being created to hold them.
 * 
 * \param following_scope Scope which will apply immediately after this call. This allows variables
 * which are about to go out of scope to be detected.
 */
TvmResult TvmFunctionLowering::run_block(Scope& scope, const TreePtr<Block>& block, const VariableSlot& slot, Scope& following_scope) {
  ScopeList sl(scope);
  for (PSI_STD::vector<TreePtr<Statement> >::const_iterator ii = block->statements.begin(), ie = block->statements.end(); ii != ie; ++ii) {
    const TreePtr<Statement>& statement = *ii;
    VariableSlot var(sl.current(), statement->value);
    TvmResult value = run(sl.current(), statement->value, var, sl.current());
    
    if (statement->functional) {
      switch (value.storage()) {
      case tvm_storage_lvalue_ref:
      case tvm_storage_rvalue_ref: {
        if (!is_primitive(sl.current(), value.type()))
          compile_context().error_throw(statement->location(), "Non-primitive type cannot be used as a funtional value");
        Tvm::ValuePtr<> loaded = m_builder.load(value.value(), statement->location());
        value = TvmResult::in_register(value.type(), tvm_storage_functional, loaded);
        break;
      }
        
      case tvm_storage_functional:
        break;

      case tvm_storage_bottom:
        var.destroy_slot();
        return TvmResult::bottom();
        
      case tvm_storage_stack: {
        if (!is_primitive(sl.current(), value.type()))
          compile_context().error_throw(statement->location(), "Non-primitive type cannot be used as a funtional value");
        Scope sc(sl.current(), statement->location(), value, var);
        Tvm::ValuePtr<> loaded = m_builder.load(value.value(), statement->location());
        value = TvmResult::in_register(value.type(), tvm_storage_functional, loaded);
        sc.cleanup();
      }
      }
    } else if (value.storage() == tvm_storage_functional) {
      m_builder.store(value.value(), var.slot(), statement->location());
      value = TvmResult::on_stack(value.type(), var.slot());
    }
    
    sl.push(new Scope(sl.current(), statement->location(), value, var));
  }
  
  TvmResult result = run(sl.current(), block->value, slot, following_scope);
  
  sl.cleanup();

  return result;
}

/**
 * \brief Create TVM structure for If-Then-Else.
 */
TvmResult TvmFunctionLowering::run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const VariableSlot& slot, Scope& following_scope) {
  Tvm::ValuePtr<> condition = run_functional(scope, if_then_else->condition);
  Tvm::ValuePtr<Tvm::Block> true_block = builder().new_block(if_then_else->true_value->location());
  Tvm::ValuePtr<Tvm::Block> false_block = builder().new_block(if_then_else->false_value->location());

  builder().cond_br(condition, true_block, false_block, if_then_else->location());
  Tvm::InstructionInsertPoint cond_insert = builder().insert_point();
  
  MergeExitList results;
  
  builder().set_insert_point(true_block);
  TvmResult true_value = run(scope, if_then_else->true_value, slot, following_scope);
  results.push_back(std::make_pair(builder().block(), true_value));
  
  builder().set_insert_point(false_block);
  TvmResult false_value = run(scope, if_then_else->false_value, slot, following_scope);
  results.push_back(std::make_pair(builder().block(), false_value));
  
  return merge_exit(scope, if_then_else->type, slot, results);
}

/**
 * \brief Create TVM structure for a jump group.
 */
TvmResult TvmFunctionLowering::run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const VariableSlot& slot, Scope& following_scope) {
  JumpMapType initial_jump_map;
  std::vector<Tvm::ValuePtr<> > parameter_types;
  for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    JumpData jd;
    jd.block = builder().new_block(ii->location());
    if ((*ii)->argument) {
      Tvm::ValuePtr<> type = run_type(scope, (*ii)->argument->type).value();
      if ((*ii)->argument_mode == result_mode_by_value) {
        // Use storage to hold the member index temporarily
        jd.storage = Tvm::FunctionalBuilder::size_value(tvm_context(), parameter_types.size(), (*ii)->location());
        parameter_types.push_back(type);
      } else {
        jd.storage = jd.block->insert_phi(Tvm::FunctionalBuilder::pointer_type(type, (*ii)->location()), (*ii)->location());
      }
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
        PSI_ASSERT(!ii->second.storage->is_type());
        ii->second.storage = Tvm::FunctionalBuilder::element_ptr(storage, ii->second.storage, ii->first->location());
      }
    }
  }

  Scope jump_scope(scope, jump_group->location(), initial_jump_map);

  MergeExitList results;
  
  TvmResult initial_value = run(jump_scope, jump_group->initial, slot, following_scope);
  Tvm::ValuePtr<Tvm::Block> group_dominator = builder().block();
  results.push_back(std::make_pair(builder().block(), initial_value));

  for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
    const JumpData& jd = jump_scope.jump_map().find(*ii)->second;
    builder().set_insert_point(m_output->new_block((*ii)->location(), group_dominator));

    TvmResult entry_result;
    if ((*ii)->argument) {
      VariableSlot entry_variable(jump_scope, (*ii)->argument);
      TvmResult entry_variable_result;
      switch ((*ii)->argument_mode) {
      case result_mode_by_value: {
        move_construct_destroy(scope, (*ii)->argument->type, entry_variable.slot(), jd.storage, (*ii)->location());
        entry_variable_result = TvmResult::on_stack((*ii)->argument->type, entry_variable.slot());
        break;
      }
      
      case result_mode_lvalue:
        PSI_ASSERT(Tvm::isa<Tvm::Phi>(jd.storage));
        entry_variable_result = TvmResult::in_register((*ii)->argument->type, tvm_storage_lvalue_ref, jd.storage);
        break;

      case result_mode_rvalue:
        PSI_ASSERT(Tvm::isa<Tvm::Phi>(jd.storage));
        entry_variable_result = TvmResult::in_register((*ii)->argument->type, tvm_storage_rvalue_ref, jd.storage);
        break;

      default: PSI_FAIL("unknown enum value");
      }

      Scope entry_scope(jump_scope, (*ii)->location(), entry_variable_result, entry_variable, (*ii)->argument);
      entry_result = run(entry_scope, (*ii)->value, slot, following_scope);
      entry_scope.cleanup();
    } else {
      entry_result = run(jump_scope, (*ii)->value, slot, following_scope);
    }
    
    results.push_back(std::make_pair(builder().block(), entry_result));
  }
  
  return merge_exit(jump_scope, jump_group->type, slot, results);
}

/**
 * Handle a jump.
 */
TvmResult TvmFunctionLowering::run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const VariableSlot&, Scope&) {
  Tvm::ValuePtr<> result_value;
  if (jump_to->argument) {
    std::pair<Scope*, Tvm::ValuePtr<> > ei = exit_info(scope, jump_to->target, jump_to->location());
    VariableSlot var(scope, jump_to->argument, ei.second);
    // Do not use var.assign here - it might try and destroy the existing stack storage
    TvmResult var_result = run(scope, jump_to->argument, var, *ei.first);
    
    switch (var_result.storage()) {
    case tvm_storage_stack:
      switch (jump_to->target->argument_mode) {
      case result_mode_lvalue:
      case result_mode_rvalue:
        compile_context().error_throw(scope.location(), "Cannot create reference to stack variable going out of scope");

      case result_mode_by_value:
        break;
        
      case result_mode_functional: {
        result_value = builder().load(var_result.value(), jump_to->location());
        Scope var_scope(scope, jump_to->location(), var_result, var);
        var_scope.cleanup();
        break;
      }
      }
      break;
        
    case tvm_storage_lvalue_ref:
      switch (jump_to->target->argument_mode) {
      case result_mode_by_value: copy_construct(scope, var_result.type(), ei.second, var_result.value(), jump_to->location()); break;
      case result_mode_lvalue:
      case result_mode_rvalue: result_value = var_result.value(); break;
      case result_mode_functional: result_value = builder().load(var_result.value(), jump_to->location()); break;
      default: PSI_FAIL("unknown enum value");
      }
      break;
      
    case tvm_storage_rvalue_ref:
      switch (jump_to->target->argument_mode) {
      case result_mode_by_value: move_construct(scope, var_result.type(), ei.second, var_result.value(), jump_to->location()); break;
      case result_mode_lvalue: compile_context().error_throw(scope.location(), "Cannot implicitly convert lvalue reference to rvalue reference"); break;
      case result_mode_rvalue: result_value = var_result.value(); break;
      case result_mode_functional: result_value = builder().load(var_result.value(), jump_to->location()); break;
      default: PSI_FAIL("unknown enum value");
      }
      break;

    case tvm_storage_functional:
      switch (jump_to->target->argument_mode) {
      case result_mode_by_value: builder().store(var_result.value(), ei.second, jump_to->location()); break;
      case result_mode_functional: result_value = var_result.value(); break;
      case result_mode_lvalue:
      case result_mode_rvalue: compile_context().error_throw(scope.location(), "Cannot convert funtional value to reference"); break;
      default: PSI_FAIL("unknown enum value");
      }
      break;
      
    case tvm_storage_bottom:
      return TvmResult::bottom();

    default: PSI_FAIL("unknown enum value");
    }
  }

  exit_to(scope, jump_to->target, jump_to->location(), result_value);
  
  return TvmResult::bottom();
}

class TvmFunctionLowering::TryFinallyCleanup : public CleanupCallback {
  TreePtr<TryFinally> m_try_finally;
  
public:
  TryFinallyCleanup(const TreePtr<TryFinally>& try_finally) : m_try_finally(try_finally) {}
  
  virtual void run(Scope& scope) {
    VariableSlot slot(scope, m_try_finally->finally_expr);
    TvmResult r = scope.shared().run(scope, m_try_finally->finally_expr, slot, scope);
    Scope child_scope(scope, m_try_finally->location(), r, slot);
    child_scope.cleanup();
  }
};

TvmResult TvmFunctionLowering::run_try_finally(Scope& scope, const TreePtr<TryFinally>& try_finally, const VariableSlot& slot, Scope& PSI_UNUSED(following_scope)) {
  TryFinallyCleanup cleanup(try_finally);
  Scope my_scope(scope, try_finally->location(), &cleanup);
  TvmResult result = run(my_scope, try_finally->try_expr, slot, my_scope);
  my_scope.cleanup();
  return result;
}

/**
 * \brief Lower a function call.
 */
TvmResult TvmFunctionLowering::run_call(Scope& scope, const TreePtr<FunctionCall>& call, const VariableSlot& var, Scope& PSI_UNUSED(following_scope)) {  
  // Build argument scope
  TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(call->target->type);
  ScopeList sl(scope);
  std::vector<Tvm::ValuePtr<> > tvm_arguments;
  for (unsigned ii = 0, ie = call->arguments.size(); ii != ie; ++ii) {
    const TreePtr<Term>& argument = call->arguments[ii];
    VariableSlot arg_var(sl.current(), argument);
    TvmResult arg_result = run(sl.current(), argument, arg_var, sl.current());
    sl.push(new Scope(sl.current(), argument->location(), arg_result, arg_var));
    
    Tvm::ValuePtr<> value;
    switch (arg_result.storage()) {
    case tvm_storage_stack:
    case tvm_storage_lvalue_ref:
      switch (ftype->parameter_types[ii].mode) {
      case parameter_mode_input:
      case parameter_mode_output:
      case parameter_mode_io: value = arg_result.value(); break;
      
      case parameter_mode_functional: {
        value = m_builder.load(arg_result.value(), argument->location());
        break;
      }
        
      case parameter_mode_rvalue: {
        VariableSlot copy_var(sl.current(), argument);
        value = copy_var.slot();
        copy_construct(scope, arg_result.type(), copy_var.slot(), arg_result.value(), argument->location());
        sl.push(new Scope(sl.current(), argument->location(), TvmResult::on_stack(argument->type, copy_var.slot()), copy_var));
      }
      }
      break;

    case tvm_storage_rvalue_ref:
      switch (ftype->parameter_types[ii].mode) {
      case parameter_mode_input:
      case parameter_mode_io:
      case parameter_mode_rvalue: value = arg_result.value(); break;
      case parameter_mode_output: compile_context().error_throw(argument->location(), "Cannot pass rvalue to output argument");
      case parameter_mode_functional: compile_context().error_throw(argument->location(), "Cannot pass rvalue to functional argument");
      }
      break;
      
    case tvm_storage_functional:
      switch (ftype->parameter_types[ii].mode) {
      case parameter_mode_input: {
        value = m_builder.alloca_(Tvm::value_cast<Tvm::PointerType>(arg_result.value()->type())->target_type(), argument->location());
        m_builder.store(arg_result.value(), value, argument->location());
        break;
      }
      
      case parameter_mode_functional: value = arg_result.value(); break;
      case parameter_mode_output: compile_context().error_throw(argument->location(), "Cannot pass functional value to output argument");
      case parameter_mode_io: compile_context().error_throw(argument->location(), "Cannot pass functional value to I/O argument");
      case parameter_mode_rvalue: compile_context().error_throw(argument->location(), "Cannot pass functional value to rvalue argument");
      }
      break;

    case tvm_storage_bottom:
      return TvmResult::bottom();
    }
    
    tvm_arguments.push_back(value);
  }
  
  if (!ftype->interfaces.empty())
    PSI_NOT_IMPLEMENTED();
  
  if ((ftype->result_mode == result_mode_by_value) && !is_primitive(sl.current(), call->type))
    tvm_arguments.push_back(var.slot());
  
  Tvm::ValuePtr<> result;
  if (TreePtr<BuiltinFunction> builtin = dyn_treeptr_cast<BuiltinFunction>(call->target)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    VariableSlot target_var(sl.current(), call->target);
    TvmResult target_result = run(sl.current(), call->target, target_var, sl.current());
    PSI_ASSERT(target_result.storage() == tvm_storage_lvalue_ref);
    result = m_builder.call(target_result.value(), tvm_arguments, call->location());
  }

  sl.cleanup();

  switch (ftype->result_mode) {
  case result_mode_by_value: return TvmResult::on_stack(call->type, var.slot());
  case result_mode_functional: return TvmResult::in_register(call->type, tvm_storage_functional, result);
  case result_mode_rvalue: return TvmResult::in_register(call->type, tvm_storage_rvalue_ref, result);
  case result_mode_lvalue: return TvmResult::in_register(call->type, tvm_storage_lvalue_ref, result);
  default: PSI_FAIL("Unknown enum value");
  }
}

bool TvmFunctionLowering::merge_exit_list_entry_bottom(const MergeExitList::value_type& el) {
  return el.second.storage() == tvm_storage_bottom;
}

TvmStorage TvmFunctionLowering::merge_storage(TvmStorage x, TvmStorage y) {
  PSI_ASSERT(x != tvm_storage_bottom);
  PSI_ASSERT(y != tvm_storage_bottom);

  switch (x) {
  case tvm_storage_stack:
    return tvm_storage_stack;
    
  case tvm_storage_lvalue_ref:
    switch (y) {
    case tvm_storage_lvalue_ref:
    case tvm_storage_rvalue_ref: return tvm_storage_lvalue_ref;
    case tvm_storage_stack:
    case tvm_storage_functional: return tvm_storage_stack;
    default: PSI_FAIL("unknown enum value");
    }
    
  case tvm_storage_rvalue_ref:
    switch (y) {
    case tvm_storage_lvalue_ref: return tvm_storage_lvalue_ref;
    case tvm_storage_rvalue_ref: return tvm_storage_rvalue_ref;
    case tvm_storage_stack:
    case tvm_storage_functional: return tvm_storage_stack;
    default: PSI_FAIL("unknown enum value");
    }
  
  case tvm_storage_functional:
    return (y == tvm_storage_functional) ? tvm_storage_functional : tvm_storage_stack;
    
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
TvmResult TvmFunctionLowering::merge_exit(Scope& scope, const TreePtr<Term>& type, const VariableSlot& slot, MergeExitList& values) {
  // Erase all bottom values
  values.erase(std::remove_if(values.begin(), values.end(), &TvmFunctionLowering::merge_exit_list_entry_bottom), values.end());
  
  if (values.size() > 1) {
    Tvm::ValuePtr<Tvm::Block> exit_block = m_output->new_block(scope.location(), scope.dominator());
    TvmStorage final_storage = values.front().second.storage();
    for (MergeExitList::const_iterator ii = boost::next(values.begin()), ie = values.end(); ii != ie; ++ii) {
      final_storage = merge_storage(final_storage, ii->second.storage());
      Tvm::InstructionBuilder(ii->first).br(exit_block, scope.location());
    }
    
    builder().set_insert_point(exit_block);

    if (final_storage == tvm_storage_stack) {
      for (MergeExitList::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii) {
        switch (ii->second.storage()) {
        case tvm_storage_stack: break;
        case tvm_storage_lvalue_ref: copy_construct(scope, type, slot.slot(), ii->second.value(), scope.location()); break;
        case tvm_storage_rvalue_ref: move_construct(scope, type, slot.slot(), ii->second.value(), scope.location()); break;
        case tvm_storage_functional: builder().store(ii->second.value(), slot.slot(), scope.location()); break;
        default: PSI_FAIL("unknown enum value");
        }
      }

      return TvmResult::on_stack(type, slot.slot());
    } else {
      Tvm::ValuePtr<> phi_type = Tvm::value_cast<Tvm::PointerType>(slot.slot()->type())->target_type();
      if (final_storage == tvm_storage_functional)
        phi_type = Tvm::FunctionalBuilder::pointer_type(phi_type, scope.location());

      Tvm::ValuePtr<Tvm::Phi> merged_value = builder().phi(phi_type, scope.location());
      for (MergeExitList::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii)
        merged_value->add_edge(ii->first, ii->second.value());
      
      return TvmResult::in_register(type, final_storage, merged_value);
    }
  } else if (values.size() == 1) {
    builder().set_insert_point(values.front().first);
    return values.front().second;
  } else {
    return TvmResult::bottom();
  }
}

/**
 * \brief Check whether a given variable goes out of scope between scope and following_scope.
 */
bool TvmFunctionLowering::going_out_of_scope(Scope& scope, const Tvm::ValuePtr<>& var, Scope& following_scope) {
  for (Scope *sc = &scope; sc != &following_scope; sc = sc->parent()) {
    PSI_ASSERT_MSG(sc, "following_scope was not a parent of scope");
    if (var == sc->variable().value())
      return true;
  }

  return false;
}

/**
 * \brief Destroy the variable belonging to a scope, if one exists.
 */
void TvmFunctionLowering::destroy_variable(Scope& scope) {
  if (scope.variable().storage() == tvm_storage_stack)
    destroy(scope, scope.variable().type(), scope.variable().value(), scope.location());
}

/// \brief Get a pointer to the CopyConstructible interface for a given type.
Tvm::ValuePtr<> TvmFunctionLowering::move_constructible_interface(const TreePtr<Term>& type) {
  PSI_NOT_IMPLEMENTED();
}

/// \brief Get a pointer to the CopyConstructible interface for a given type.
Tvm::ValuePtr<> TvmFunctionLowering::copy_constructible_interface(const TreePtr<Term>& type) {
  PSI_NOT_IMPLEMENTED();
}

/**
 * \brief Determine whether a type is primitive, in which case MoveConstructible an CopyConstructible interfaces can be short-cut.
 */
bool TvmFunctionLowering::is_primitive(Scope& scope, const TreePtr<Term>& type) {
  FunctionalBuilderCallback cb(&scope);
  TvmFunctionalBuilder builder(&compile_context(), &tvm_context(), &cb);
  return builder.is_primitive(type);
}

/// \brief Generate default constructor call
void TvmFunctionLowering::default_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const SourceLocation& location) {
  if (is_primitive(scope, type)) {
    // Should this be undef?
    builder().store(Tvm::FunctionalBuilder::zero(Tvm::value_cast<Tvm::PointerType>(dest->type())->target_type(), location), dest, location);
    return;
  }
  
  Tvm::ValuePtr<> mc = move_constructible_interface(type);
  Tvm::ValuePtr<> fp = Tvm::FunctionalBuilder::element_ptr(mc, MoveConstructible::m_construct, location);
  builder().call2(fp, mc, dest, location);
}

/// \brief Generate copy constructor call
void TvmFunctionLowering::copy_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {  
  if (is_primitive(scope, type)) {
    Tvm::ValuePtr<> value = builder().load(src, location);
    builder().store(value, dest, location);
    return;
  }

  Tvm::ValuePtr<> mc = copy_constructible_interface(type);
  Tvm::ValuePtr<> fp = Tvm::FunctionalBuilder::element_ptr(mc, CopyConstructible::m_copy, location);
  builder().call3(fp, mc, dest, src, location);
}

/// \brief Generate move constructor call
void TvmFunctionLowering::move_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  if (is_primitive(scope, type)) {
    Tvm::ValuePtr<> value = builder().load(src, location);
    builder().store(value, dest, location);
    return;
  }
  
  Tvm::ValuePtr<> mc = move_constructible_interface(type);
  Tvm::ValuePtr<> fp = Tvm::FunctionalBuilder::element_ptr(mc, MoveConstructible::m_move, location);
  builder().call3(fp, mc, dest, src, location);
}

///  \brief Generate destructor call
void TvmFunctionLowering::destroy(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& ptr, const SourceLocation& location) {
  if (is_primitive(scope, type))
    return;
  
  Tvm::ValuePtr<> mc = move_constructible_interface(type);
  Tvm::ValuePtr<> fp = Tvm::FunctionalBuilder::element_ptr(mc, MoveConstructible::m_destroy, location);
  builder().call2(fp, mc, ptr, location);
}

/**
 * \brief Generate a move constructor call followed by a destructor call on the source.
 * 
 * It is expected that this can be optimised by merging the two calls. However, currently
 * this is not done and this funtion simply calls move_construct() followed by destroy().
 */
void TvmFunctionLowering::move_construct_destroy(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location) {
  move_construct(scope, type, dest, src, location);
  destroy(scope, type, src, location);
}
}
}
