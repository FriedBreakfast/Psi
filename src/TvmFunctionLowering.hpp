#ifndef HPP_PSI_TVM_FUNCTION_LOWERING
#define HPP_PSI_TVM_FUNCTION_LOWERING

#include "SharedMap.hpp"
#include "TvmLowering.hpp"
#include "Tvm/InstructionBuilder.hpp"

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
  class ConstructorCleanup;

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
    virtual ~CleanupCallback();
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
    bool m_cleanup_except_only;
    bool m_cleanup_owner;
    
    void init(Scope& parent);

  public:
    Scope(TvmFunctionLowering *shared, const SourceLocation& location);
    Scope(Scope& parent, const SourceLocation& location, const TvmResult& result, VariableSlot& slot, const TreePtr<>& key=TreePtr<>());
    Scope(Scope& parent, const SourceLocation& location, CleanupCallback *cleanup, bool cleanup_except_only);
    Scope(Scope& parent, const SourceLocation& location, std::auto_ptr<CleanupCallback>& cleanup, bool cleanup_except_only);
    Scope(Scope& parent, const SourceLocation& location, const JumpMapType& initial_jump_map);
    Scope(Scope& parent, const SourceLocation& location, const ArrayPtr<VariableMapType::value_type>& new_variables);
    ~Scope();
    
    CompileContext& compile_context() {return m_shared->compile_context();}

    TvmFunctionLowering& shared() const {return *m_shared;}
    Scope *parent() {return m_parent;}
    const Tvm::ValuePtr<Tvm::Block>& dominator() const {return m_dominator;}
    const SourceLocation& location() const {return m_location;}
    const JumpMapType& jump_map() const {return m_jump_map;}
    const VariableMapType& variables() const {return m_variables;}
    TvmResult variable() const {return m_variable;}
    
    bool has_cleanup(bool except) const {return (m_cleanup && (except || !m_cleanup_except_only)) || (m_variable.storage() == tvm_storage_stack);}
    void cleanup(bool except);
  };
  
  class ScopeList {
    boost::ptr_vector<Scope> m_list;
    Scope *m_current;

    void pop();
    
  public:
    ScopeList(Scope& parent) : m_current(&parent) {}
    void push(Scope *scope) {m_list.push_back(scope); m_current = scope;}
    Scope& current() {return *m_current;}
    void cleanup(bool except);
  };

  TvmCompiler& tvm_compiler() {return *m_tvm_compiler;}
  CompileContext& compile_context() {return m_tvm_compiler->compile_context();}
  Tvm::Context& tvm_context() {return m_output->context();}

  bool going_out_of_scope(Scope& scope, const Tvm::ValuePtr<>& var, Scope& following_scope);
  
  bool is_primitive(Scope& scope, const TreePtr<Term>& type);
  bool is_register(Scope& scope, const TreePtr<Term>& type);
  
  void object_initialize_default(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location);
  void object_initialize_term(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location);
  void object_initialize_move(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location);
  void object_initialize_copy(ScopeList& scope_list, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location);
  void object_assign_default(Scope& scope, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location);
  void object_assign_term(Scope& scope, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location);
  void object_assign_move(Scope& scope, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location);
  void object_assign_copy(Scope& scope, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location);
  void object_destroy(Scope& scope, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location);
  
  void copy_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void move_construct(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void move_construct_destroy(Scope& scope, const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);

  void run_void(Scope& scope, const TreePtr<Term>& term);
  TvmResult run(Scope& scope, const TreePtr<Term>& term, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_block(Scope& scope, const TreePtr<Block>& block, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_if_then_else(Scope& scope, const TreePtr<IfThenElse>& if_then_else, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_jump_group(Scope& scope, const TreePtr<JumpGroup>& jump_group, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_jump(Scope& scope, const TreePtr<JumpTo>& jump_to, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_try_finally(Scope& scope, const TreePtr<TryFinally>& try_finally, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_call(Scope& scope, const TreePtr<FunctionCall>& call, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_initialize(Scope& scope, const TreePtr<InitializePointer>& initialize, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_assign(Scope& scope, const TreePtr<AssignPointer>& assign, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_finalize(Scope& scope, const TreePtr<FinalizePointer>& finalize, const VariableSlot& slot, Scope& following_scope);
  TvmResult run_constructor(Scope& scope, const TreePtr<Term>& value, const VariableSlot& slot, Scope& following_scope);
  
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
}
}

#endif
