#ifndef HPP_PSI_TVM_FUNCTION_LOWERING
#define HPP_PSI_TVM_FUNCTION_LOWERING

#include "SharedMap.hpp"
#include "TvmLowering.hpp"
#include "Tvm/InstructionBuilder.hpp"

#include <boost/ptr_container/ptr_vector.hpp>

namespace Psi {
namespace Compiler {
class TvmFunctionBuilder;
class TvmCleanup;
typedef boost::shared_ptr<TvmCleanup> TvmCleanupPtr;

struct TvmJumpData {
  Tvm::ValuePtr<Tvm::Block> block;
  
  /**
    * Holds either a stack pointer if the jump target parameter is stored on the stack,
    * or a reference to a PHI node.
    */
  Tvm::ValuePtr<> storage;
};

/**
 * Value type of list of interface implementations visible in the current scope.
 */
struct TvmGeneratedImplementation {
  TreePtr<Interface> interface;
  PSI_STD::vector<TreePtr<Term> > parameters;
  TvmResult result;
};

struct TvmFunctionState {
  typedef boost::shared_ptr<TvmCleanup> CleanupPtr;
  typedef SharedMap<TreePtr<>, TvmResult> VariableMapType;
  typedef SharedList<TreePtr<IntroduceImplementation> > LocalImplementationList;
  typedef SharedList<TvmGeneratedImplementation> GeneratedImplemenationList;
  typedef std::map<TreePtr<JumpTarget>, TvmJumpData> JumpMapType;
  
  TvmScopePtr scope;
  LocalImplementationList implementation_list;
  GeneratedImplemenationList generated_implementation_list;
  CleanupPtr cleanup;
  JumpMapType jump_map;
};

/**
 * \brief Base class for cleanup blocks during function generation.
 */
class TvmCleanup {
  friend class TvmFunctionBuilder;
  
  TvmFunctionState m_state;
  Tvm::ValuePtr<Tvm::Block> m_dominator;
  bool m_except_only;
  SourceLocation m_location;

  TvmFunctionState::JumpMapType m_jump_map_normal;
  TvmFunctionState::JumpMapType m_jump_map_exceptional;
  
public:
  TvmCleanup(bool except_only, const SourceLocation& location);
  const SourceLocation& location() const {return m_location;}
  virtual void run(TvmFunctionBuilder& builder) const = 0;
};

class StackFreeCleanup : public TvmCleanup {
  Tvm::ValuePtr<> m_stack_alloc;
  
public:
  StackFreeCleanup(const Tvm::ValuePtr<>& stack_alloc, const SourceLocation& location);
  virtual void run(TvmFunctionBuilder& builder) const;
};

class DestroyCleanup : public TvmCleanup {
  Tvm::ValuePtr<> m_slot;
  TreePtr<Term> m_type;
  
public:
  DestroyCleanup(const Tvm::ValuePtr<>& slot, const TreePtr<Term>& type, const SourceLocation& location);
  virtual void run(TvmFunctionBuilder& builder) const;
};

/**
 * \class TvmFunctionLowering
 * 
 * Converts a Function to a Tvm::Function.
 * 
 * Variable lifecycles are tracked by "following scope" pointers. The scope which will
 * be current immediately after the current term is tracked, so that variables which
 * are about to go out of scope can be detected.
 */
class TvmFunctionBuilder : public TvmFunctionalBuilder {
  struct InstructionLowering;
  class LifecycleConstructorCleanup;
  
  TvmObjectCompilerBase *m_tvm_compiler;
  TreePtr<Module> m_module;
  std::set<TreePtr<ModuleGlobal> > *m_dependencies;
  Tvm::ValuePtr<Tvm::Function> m_output;
  TreePtr<JumpTarget> m_return_target;
  Tvm::ValuePtr<> m_return_storage;
  Tvm::InstructionBuilder m_builder;

  Tvm::ValuePtr<> exit_storage(const TreePtr<JumpTarget>& target, const SourceLocation& location);
  void exit_to(const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value);
  void cleanup_to(const TvmCleanupPtr& top);

  TvmFunctionState m_state;
  Tvm::ValuePtr<> m_current_result_storage;

  void push_cleanup(const TvmCleanupPtr& cleanup);
  TvmResult get_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters,
                               const SourceLocation& location, const TreePtr<Implementation>& maybe_implementation=TreePtr<Implementation>());
  
  struct DominatorState {
    Tvm::ValuePtr<Tvm::Block> block;
    TvmFunctionState state;
  };
  
  DominatorState dominator_state();
  
  struct MergeExitEntry {
    TvmResult value;
    TermMode mode;
    DominatorState state;
    MergeExitEntry(const TvmResult& value_, TermMode mode_, const DominatorState& state_)
    : value(value_), mode(mode_), state(state_) {}
  };
  
  typedef std::vector<MergeExitEntry> MergeExitList;
  static bool merge_exit_list_entry_bottom(const MergeExitEntry& el);
  TvmResult merge_exit(const TreePtr<Term>& type, TermMode mode, MergeExitList& values, const DominatorState& dominator, const SourceLocation& location);
  
public:
  TvmFunctionBuilder(TvmObjectCompilerBase& tvm_compiler, const TreePtr<Module>& module, std::set<TreePtr<ModuleGlobal> >& dependencies);
  void run_function(const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output);
  void run_init(const TreePtr<Term>& body, const Tvm::ValuePtr<Tvm::Function>& output);
  void build_void(const TreePtr<Term>& term);

  virtual TvmResult build(const TreePtr<Term>& term);
  virtual TvmResult build_generic(const TreePtr<GenericType>& generic);
  virtual TvmResult build_global(const TreePtr<Global>& global);
  virtual TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>& global);

  /**
   * \brief Get the instruction builder for this function.
   * 
   * Note that the insertion pointer of this builder is modified throughout
   * the lowering process, and its state must be maintained carefully.
   */
  Tvm::InstructionBuilder& builder() {return m_builder;}

  TvmResult build_instruction(const TreePtr<Term>& term);
  
  enum ConstructMode {
    /// \brief Initialize a value
    construct_initialize,
    /// \brief Initialize a value and push a destructor onto the context heap
    construct_initialize_destroy,
    /// \brief Assign a value
    construct_assign,
    /// \brief Create interfaces required for given constructor
    construct_interfaces
  };
  
  bool object_construct_default(ConstructMode mode, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location);
  bool object_construct_term(ConstructMode mode, const Tvm::ValuePtr<>& dest, const TreePtr<Term>& value, const SourceLocation& location);
  bool object_construct_move_copy(ConstructMode mode, bool move, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const TreePtr<Term>& type, const SourceLocation& location);
  void object_destroy(const Tvm::ValuePtr<>& dest, const TreePtr<Term>& type, const SourceLocation& location);
  
  void copy_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void move_construct(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
  void move_construct_destroy(const TreePtr<Term>& type, const Tvm::ValuePtr<>& dest, const Tvm::ValuePtr<>& src, const SourceLocation& location);
};
}
}

#endif
