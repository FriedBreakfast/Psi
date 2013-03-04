#include "TvmFunctionLowering.hpp"
#include "TreeMap.hpp"
#include "TermBuilder.hpp"
#include "Tvm/FunctionalBuilder.hpp"

namespace Psi {
namespace Compiler {
struct TvmFunctionBuilder::InstructionLowering {
  class StackRestoreCleanup : public TvmCleanup {
    Tvm::ValuePtr<> m_stack_ptr;
    
  public:
    StackRestoreCleanup(const Tvm::ValuePtr<>& stack_ptr, const SourceLocation& location)
    : TvmCleanup(false, location), m_stack_ptr(stack_ptr) {}
    
    virtual void run(TvmFunctionBuilder& builder) const {
      builder.builder().stack_restore(m_stack_ptr, location());
    }
  };
  
  class DestroyCleanup : public TvmCleanup {
    Tvm::ValuePtr<> m_slot, m_stack_ptr;
    TreePtr<Term> m_type;
    
  public:
    DestroyCleanup(const Tvm::ValuePtr<>& slot, const TreePtr<Term>& type, const Tvm::ValuePtr<>& stack_ptr, const SourceLocation& location)
    : TvmCleanup(false, location), m_slot(slot), m_stack_ptr(stack_ptr), m_type(type) {}
    
    virtual void run(TvmFunctionBuilder& builder) const {
      builder.object_destroy(m_slot, m_type, location());
      builder.builder().stack_restore(m_stack_ptr, location());
    }
  };
  
  static TvmResult run_interface_value(TvmFunctionBuilder& builder, const TreePtr<InterfaceValue>& interface_value) {
    return builder.get_implementation(interface_value->interface, interface_value->parameters,
                                      interface_value.location(), interface_value->implementation);
  }
  
  static TvmResult run_introduce_implementation(TvmFunctionBuilder& builder, const TreePtr<IntroduceImplementation>& introduce_impl) {
    TvmFunctionState::LocalImplementationList impl_list = builder.m_state.implementation_list;
    builder.m_state.implementation_list.push_front(introduce_impl);
    TvmResult inner = builder.build(introduce_impl->value);
    builder.m_state.implementation_list = impl_list;
    return inner;
  }
  
  /**
  * Run the highest level of a context. In this function new local variables (in Blocks) may be created
  * in this context, rather than a child context being created to hold them.
  * 
  * \param following_scope Scope which will apply immediately after this call. This allows variables
  * which are about to go out of scope to be detected.
  */
  static TvmResult run_block(TvmFunctionBuilder& builder, const TreePtr<Block>& block) {
    TvmCleanupPtr initial_cleanup = builder.m_state.cleanup;
    Tvm::ValuePtr<> initial_result_storage = builder.m_current_result_storage;
    builder.m_current_result_storage.reset();
    
    for (PSI_STD::vector<TreePtr<Statement> >::const_iterator ii = block->statements.begin(), ie = block->statements.end(); ii != ie; ++ii) {
      const TreePtr<Statement>& statement = *ii;

      TvmCleanupPtr before_statement_cleanup = builder.m_state.cleanup;
      TvmResult value;
      
      Tvm::ValuePtr<> stack_slot, stack_ptr;
      if ((statement->mode == statement_mode_value) || ((statement->mode == statement_mode_destroy) && !statement->value->is_functional())) {
        TvmResult type = builder.build(statement->value->result_type.type);
        stack_ptr = builder.builder().stack_save(statement->location());
        stack_slot = builder.builder().alloca_(type.value, statement->location());
        if (builder.object_initialize_term(stack_slot, statement->value, true, statement->location()))
          value = TvmResult(builder.m_state.scope.get(), stack_slot);
        else
          value = TvmResult::bottom();
      } else {
        switch (statement->mode) {
        case statement_mode_functional: {
          // Let FunctionalEvaluate handler handle this
          value = builder.build(TermBuilder::to_functional(statement->value, statement->location()));
          break;
        }
        
        case statement_mode_ref: {
          PSI_ASSERT((statement->value->result_type.mode == term_mode_lref) || (statement->value->result_type.mode == term_mode_rref));
          value = builder.build(statement->value);
          break;
        }
        
        case statement_mode_destroy: {
          value = builder.build(statement->value);
          break;
        }
            
        default: PSI_FAIL("Unrecognised statement storage mode");
        }
      }
      
      if (value.is_bottom()) {
        // Evaluation returns no result so cannot complete.
        // No need to generate normal cleanup exit in this case.
        builder.m_state.cleanup = initial_cleanup;
        builder.m_current_result_storage = initial_result_storage;
        return TvmResult::bottom();
      }

      builder.cleanup_to(before_statement_cleanup);
      
      if (!statement->value->is_functional()) {
        PSI_ASSERT(stack_slot && stack_ptr);
        builder.push_cleanup(boost::make_shared<DestroyCleanup>(stack_slot, statement->result_type.type, stack_ptr, statement->location()));
      }
      
      if (statement->mode != statement_mode_destroy) {
        builder.m_state.scope->put(statement, value);
      } else {
        builder.m_state.scope->put(statement, TvmResult::bottom());
        builder.cleanup_to(before_statement_cleanup);
      }
    }
    
    builder.m_current_result_storage = initial_result_storage;
    TvmResult result = builder.build(block->value);
    builder.cleanup_to(initial_cleanup);
    return result;
  }

  /**
  * \brief Create TVM structure for If-Then-Else.
  */
  static TvmResult run_if_then_else(TvmFunctionBuilder& builder, const TreePtr<IfThenElse>& if_then_else) {
    PSI_ASSERT(if_then_else->condition->result_type.mode == term_mode_value);
    TvmResult condition = builder.build(if_then_else->condition);
    if (condition.is_bottom())
      return TvmResult::bottom();
    
    Tvm::ValuePtr<Tvm::Block> true_block = builder.builder().new_block(if_then_else->true_value->location());
    Tvm::ValuePtr<Tvm::Block> false_block = builder.builder().new_block(if_then_else->false_value->location());

    builder.builder().cond_br(condition.value, true_block, false_block, if_then_else->location());
    
    MergeExitList results;
    DominatorState dominator_state = builder.dominator_state();
    
    builder.builder().set_insert_point(true_block);
    TvmResult true_value = builder.build(if_then_else->true_value);
    results.push_back(MergeExitEntry(true_value.value, if_then_else->true_value->result_type.mode, builder.dominator_state()));
    builder.m_state = dominator_state.state;

    builder.builder().set_insert_point(false_block);
    TvmResult false_value = builder.build(if_then_else->false_value);
    results.push_back(MergeExitEntry(false_value.value, if_then_else->false_value->result_type.mode, builder.dominator_state()));
    builder.m_state = dominator_state.state;
    
    return builder.merge_exit(if_then_else->result_type, results, dominator_state, if_then_else->location());
  }

  /**
   * \brief Create TVM structure for a jump group.
   */
  static TvmResult run_jump_group(TvmFunctionBuilder& builder, const TreePtr<JumpGroup>& jump_group) {
    TvmFunctionState::JumpMapType jump_map;
    std::vector<Tvm::ValuePtr<> > parameter_types;
    for (PSI_STD::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
      TvmJumpData jd;
      jd.block = builder.builder().new_block(ii->location());
      if ((*ii)->argument) {
        TvmResult type = builder.build((*ii)->argument->result_type.type);
        if ((*ii)->argument_mode == result_mode_by_value) {
          // Use storage to hold the member index, which will be used to get the member later
          jd.storage = Tvm::FunctionalBuilder::size_value(builder.tvm_context(), parameter_types.size(), (*ii)->location());
          parameter_types.push_back(type.value);
        } else {
          Tvm::ValuePtr<> phi_type;
          if ((*ii)->argument_mode == result_mode_functional)
            phi_type = type.value;
          else
            phi_type = Tvm::FunctionalBuilder::pointer_type(type.value, (*ii)->location());
          jd.storage = jd.block->insert_phi(phi_type, (*ii)->location());
        }
      }
      jump_map.insert(std::make_pair(*ii, jd));
    }
    
    // Construct a union of all jump target parameter types,
    // in order to allocate the least storage possible.
    Tvm::ValuePtr<> storage;
    if (!parameter_types.empty()) {
      Tvm::ValuePtr<> storage_type = Tvm::FunctionalBuilder::union_type(builder.tvm_context(), parameter_types, jump_group->location());
      Tvm::ValuePtr<> storage = builder.builder().alloca_(storage_type, jump_group->location());
      Tvm::ValuePtr<> stack_ptr = builder.builder().stack_save(jump_group->location());
      builder.push_cleanup(boost::make_shared<StackRestoreCleanup>(stack_ptr, jump_group->location()));
    }

    TvmFunctionState::JumpMapType original_jump_map = builder.m_state.jump_map;
    for (TvmFunctionState::JumpMapType::iterator ii = jump_map.begin(), ie = jump_map.end(); ii != ie; ++ii) {
      if (ii->first->argument && (ii->first->argument_mode == result_mode_by_value)) {
        PSI_ASSERT(!ii->second.storage->is_type());
        ii->second.storage = Tvm::FunctionalBuilder::element_ptr(storage, ii->second.storage, ii->first->location());
      }
      builder.m_state.jump_map.insert(*ii);
    }

    DominatorState dominator = builder.dominator_state();

    TvmResult initial_value = builder.build(jump_group->initial);
    MergeExitList results;
    results.push_back(MergeExitEntry(initial_value.value, jump_group->initial->result_type.mode, builder.dominator_state()));

    for (std::vector<TreePtr<JumpTarget> >::const_iterator ii = jump_group->entries.begin(), ie = jump_group->entries.end(); ii != ie; ++ii) {
      const TvmJumpData& jd = jump_map.find(*ii)->second;

      builder.m_state = dominator.state;
      builder.builder().set_insert_point(dominator.block);
      Tvm::ValuePtr<Tvm::Block> block = builder.builder().new_block((*ii)->location());
      builder.builder().set_insert_point(block);

      if ((*ii)->argument)
        builder.m_state.scope->put((*ii)->argument, TvmResult(builder.m_state.scope.get(), jd.storage));
      
      TvmResult entry_result = builder.build((*ii)->value);
      results.push_back(MergeExitEntry(entry_result.value, (*ii)->value->result_type.mode, builder.dominator_state()));
    }
    
    builder.m_state.jump_map = original_jump_map;
    return builder.merge_exit(jump_group->result_type, results, dominator, jump_group->location());
  }

  /**
   * Handle a jump.
   */
  static TvmResult run_jump(TvmFunctionBuilder& builder, const TreePtr<JumpTo>& jump_to) {
    Tvm::ValuePtr<> result_value;
    if (jump_to->argument) {
      Tvm::ValuePtr<> exit_storage = builder.exit_storage(jump_to->target, jump_to->location());
      // Do not use var.assign here - it might try and destroy the existing stack storage
      switch (jump_to->target->argument_mode) {
      case result_mode_by_value:
        if (!builder.object_initialize_term(exit_storage, jump_to->argument, true, jump_to->location()))
          return TvmResult::bottom();
        break;
      
      case result_mode_functional: {
        // Let FunctionalEvaluate handler handle this
        result_value = builder.build(TermBuilder::to_functional(jump_to->argument, jump_to->location())).value;
        break;
      }
      
      case result_mode_lvalue: {
        PSI_ASSERT((jump_to->argument->result_type.mode == term_mode_lref) || (jump_to->argument->result_type.mode == term_mode_rref));
        result_value = builder.build(jump_to->argument).value;
        break;
      }
      
      case result_mode_rvalue: {
        PSI_ASSERT(jump_to->argument->result_type.mode == term_mode_rref);
        result_value = builder.build(jump_to->argument).value;
        break;
      }
          
      default: PSI_FAIL("Unrecognised statement storage mode");
      }
    }

    builder.exit_to(jump_to->target, jump_to->location(), result_value);
    
    return TvmResult::bottom();
  }

  class TryFinallyCleanup : public TvmCleanup {
    TreePtr<TryFinally> m_try_finally;
    
  public:
    TryFinallyCleanup(const TreePtr<TryFinally>& try_finally)
    : TvmCleanup(false, try_finally->location()), m_try_finally(try_finally) {}
    
    virtual void run(TvmFunctionBuilder& builder) const {
      builder.build_void(m_try_finally->finally_expr);
    }
  };

  static TvmResult run_try_finally(TvmFunctionBuilder& builder, const TreePtr<TryFinally>& try_finally) {
    TvmCleanupPtr cleanup = builder.m_state.cleanup;
    builder.push_cleanup(boost::make_shared<TryFinallyCleanup>(try_finally));
    TvmResult result = builder.build(try_finally->try_expr);
    builder.cleanup_to(cleanup);
    return result;
  }

  /**
   * \brief Lower a function call.
   */
  static TvmResult run_call(TvmFunctionBuilder& builder, const TreePtr<FunctionCall>& call) {
    // Build argument scope
    TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(call->target->result_type.type);
    std::vector<Tvm::ValuePtr<> > tvm_arguments;
    bool object_stack_saved = false;
    for (unsigned ii = 0, ie = call->arguments.size(); ii != ie; ++ii) {
      const TreePtr<Term>& argument = call->arguments[ii];
      
      TvmResult arg_result;
      if ((argument->result_type.mode == term_mode_value) && (argument->result_type.type->result_type.type_mode == type_mode_complex)) {
        TvmResult type = builder.build(argument->result_type.type);
        if (!object_stack_saved) {
          Tvm::ValuePtr<> ptr = builder.builder().stack_save(call->location());
          builder.push_cleanup(boost::make_shared<StackRestoreCleanup>(ptr, call->location()));
          object_stack_saved = true;
        }
        Tvm::ValuePtr<> stack_slot = builder.builder().alloca_(type.value, call->location());
        if (!builder.object_initialize_term(stack_slot, argument, false, call->location()))
          return TvmResult::bottom();
        arg_result = TvmResult(builder.m_state.scope.get(), stack_slot);
      } else {
        arg_result = builder.build(argument);
        if (arg_result.is_bottom())
          return TvmResult::bottom();
      }

      tvm_arguments.push_back(arg_result.value);
    }
    
    if (!ftype->interfaces.empty())
      PSI_NOT_IMPLEMENTED();

    Tvm::ValuePtr<> result;
    if (TreePtr<BuiltinFunction> builtin = dyn_treeptr_cast<BuiltinFunction>(call->target)) {
      PSI_ASSERT(ftype->result_mode == result_mode_functional);
      PSI_NOT_IMPLEMENTED();
    } else {
      TvmResult target_result = builder.build(call->target);
      PSI_ASSERT(call->target->result_type.mode == term_mode_lref);
      
      // This is here so that it is evaluated before the stack pointer is saved
      TvmResult result_type = builder.build(call->result_type.type);
      
      Tvm::ValuePtr<> stack_ptr;
      for (std::size_t ii = 0, ie = call->arguments.size(); ii != ie; ++ii) {
        switch (ftype->parameter_types[ii].mode) {
        case parameter_mode_output:
        case parameter_mode_io:
        case parameter_mode_input: {
          const TreePtr<Term>& argument = call->arguments[ii];
          if ((argument->result_type.mode == term_mode_value) && (argument->result_type.type->result_type.type_mode == type_mode_primitive)) {
            Tvm::ValuePtr<>& register_value = tvm_arguments[ii];
            if (!stack_ptr)
              stack_ptr = builder.builder().stack_save(call->location());
            Tvm::ValuePtr<> slot = builder.builder().alloca_(register_value->type(), call->location());
            builder.builder().store(register_value, slot, call->location());
            register_value = slot;
          }
          break;
        }
          
        default:
          break;
        }
      }
      
      Tvm::ValuePtr<> result_temporary, result_stack_ptr;
      if (ftype->result_mode == result_mode_by_value) {
        // Note that at a system level this parameter will be first in the list, and should be marked sret
        if (call->result_type.type->result_type.type_mode == type_mode_complex) {
          tvm_arguments.push_back(builder.m_current_result_storage);
        } else {
          if (!stack_ptr)
            stack_ptr = builder.builder().stack_save(call->location());
          result_temporary = builder.builder().alloca_(result_type.value, call->location());
          tvm_arguments.push_back(result_temporary);
        }
      }
      
      result = builder.builder().call(target_result.value, tvm_arguments, call->location());
      if (result_temporary)
        result = builder.builder().load(result_temporary, call->location());
      else if (ftype->result_mode == result_mode_by_value)
        result = builder.m_current_result_storage;
      
      if (stack_ptr)
        builder.builder().stack_restore(stack_ptr, call->location());
      
      return TvmResult(builder.m_state.scope.get(), result);
    }
  }
  
  static TvmResult run_initialize(TvmFunctionBuilder& builder, const TreePtr<InitializePointer>& initialize) {
    TvmResult dest_ptr = builder.build(initialize->target_ptr);
    builder.object_initialize_term(dest_ptr.value, initialize->assign_value, true, initialize.location());
    return builder.build(initialize->inner);
  }

  static TvmResult run_assign(TvmFunctionBuilder& builder, const TreePtr<AssignPointer>& assign) {
    TvmResult dest_ptr = builder.build(assign->target_ptr);
    builder.object_assign_term(dest_ptr.value, assign->assign_value, assign.location());
    return TvmResult(builder.m_state.scope.get(), Tvm::FunctionalBuilder::empty_value(builder.tvm_context(), assign.location()));
  }

  static TvmResult run_finalize(TvmFunctionBuilder& builder, const TreePtr<FinalizePointer>& finalize) {
    TvmResult dest_ptr = builder.build(finalize->target_ptr);
    TreePtr<PointerType> ptr_type = treeptr_cast<PointerType>(finalize->target_ptr);
    builder.object_destroy(dest_ptr.value, ptr_type->target_type, finalize.location());
    return TvmResult(builder.m_state.scope.get(), Tvm::FunctionalBuilder::empty_value(builder.tvm_context(), finalize.location()));
  }

  typedef TreeOperationMap<Term, TvmResult, TvmFunctionBuilder&> CallbackMap;
  static CallbackMap callback_map;

  static CallbackMap::Initializer callback_map_initializer() {
    return CallbackMap::initializer()
      .add<InterfaceValue>(run_interface_value)
      .add<IntroduceImplementation>(run_introduce_implementation)
      .add<Block>(run_block)
      .add<IfThenElse>(run_if_then_else)
      .add<JumpGroup>(run_jump_group)
      .add<JumpTo>(run_jump)
      .add<TryFinally>(run_try_finally)
      .add<FunctionCall>(run_call)
      .add<InitializePointer>(run_initialize)
      .add<AssignPointer>(run_assign)
      .add<FinalizePointer>(run_finalize);
  }
};

TvmFunctionBuilder::InstructionLowering::CallbackMap
  TvmFunctionBuilder::InstructionLowering::callback_map(TvmFunctionBuilder::InstructionLowering::callback_map_initializer());
}
}