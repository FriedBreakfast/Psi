#include "Compiler.hpp"
#include "Tree.hpp"
#include "Interface.hpp"
#include "TvmLowering.hpp"
#include "TvmFunctionLowering.hpp"
#include "Array.hpp"
#include "TermBuilder.hpp"

#include "Tvm/Core.hpp"
#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Aggregate.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/make_shared.hpp>
#include <boost/next_prior.hpp>

namespace Psi {
namespace Compiler {
TvmFunctionBuilder::TvmFunctionBuilder(CompileContext& compile_context, Tvm::Context& tvm_context)
: TvmFunctionalBuilder(compile_context, tvm_context) {
}

void TvmFunctionBuilder::run_body(TvmCompiler *tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output) {
  m_state.scope = tvm_compiler->scope();
  m_output = output;
  m_module = function->module;
  m_tvm_compiler = tvm_compiler;
  
  TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(function->result_type.type);
  
  if (ftype->result_mode == result_mode_by_value)
    m_return_storage = output->parameters().back();

  // We need this to be non-NULL
  m_return_target = function->return_target;
  if (!m_return_target)
    m_return_target = TermBuilder::exit_target(ftype->result_type, ftype->result_mode, function.location());
  
  Tvm::ValuePtr<Tvm::Block> entry_block = output->new_block(function.location());
  m_builder.set_insert_point(entry_block);

  TreePtr<JumpTo> exit_jump = TermBuilder::jump_to(m_return_target, function->body(), function.location());
  build(exit_jump);
}

/**
 * \brief Lower a function.
 * 
 * Constructs a TvmFunctionLowering object and runs it.
 */
void tvm_lower_function(TvmCompiler& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output) {
  TvmFunctionBuilder(tvm_compiler.compile_context(), tvm_compiler.tvm_context()).run_body(&tvm_compiler, function, output);
}

Tvm::ValuePtr<> TvmFunctionBuilder::exit_storage(const TreePtr<JumpTarget>& target, const SourceLocation& location) {
  if (target == m_return_target)
    return m_return_storage;
  
  for (TvmFunctionState *state = &m_state; ; state = &state->cleanup->m_state) {
    TvmFunctionState::JumpMapType::const_iterator it;
    
    const TvmFunctionState::JumpMapType& state_jump_map = state->jump_map;
    if ((it = state_jump_map.find(target)) != state_jump_map.end())
      return it->second.storage;
    
    if (!state->cleanup)
      compile_context().error_throw(location, "Jumped to when not in scope");
    
    const TvmFunctionState::JumpMapType& normal_map = state->cleanup->m_jump_map_normal;
    if ((it = normal_map.find(target)) != normal_map.end())
      return it->second.storage;

    const TvmFunctionState::JumpMapType& exceptional_map = state->cleanup->m_jump_map_exceptional;
    if ((it = exceptional_map.find(target)) != exceptional_map.end())
      return it->second.storage;
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
 * \param target Jump target. This will be NULL for a throw passing
 * through the function.
 */
void TvmFunctionBuilder::exit_to(const TreePtr<JumpTarget>& target, const SourceLocation& location, const Tvm::ValuePtr<>& return_value) {
  PSI_ASSERT(bool(return_value) != (target->argument_mode == result_mode_by_value));
  // This will be modified as we pass through PHI nodes
  Tvm::ValuePtr<> phi_value = return_value;
  Tvm::ValuePtr<> storage = exit_storage(target, location);
  
  SourceLocation variable_location = location;
  while (true) {
    TvmFunctionState::JumpMapType::const_iterator jump_map_it;
    
    const TvmFunctionState::JumpMapType& state_jump_map = target ? m_state.cleanup->m_jump_map_normal : m_state.cleanup->m_jump_map_exceptional;
    if ((jump_map_it = state_jump_map.find(target)) != state_jump_map.end()) {
      builder().br(jump_map_it->second.block, variable_location);
      if (phi_value)
        Tvm::value_cast<Tvm::Phi>(jump_map_it->second.storage)->add_edge(builder().block(), phi_value);
      return;
    } else if (!m_state.cleanup) {
      if (!target) {
        // Rethrow exit... need to generate re-throw code
        PSI_NOT_IMPLEMENTED();
        return;
      } else if (target == m_return_target) {
        if (return_value) {
          if (m_return_storage) {
            builder().store(phi_value, m_return_storage, location);
            builder().return_void(location);
          } else {
            builder().return_(phi_value, location);
          }
        } else {
          builder().return_void(location);
        }
        return;
      } else {
        compile_context().error_throw(location, "Jump target is not in scope.");
      }
    } else {
      TvmFunctionState::JumpMapType& cleanup_jump_map = target ? m_state.cleanup->m_jump_map_normal : m_state.cleanup->m_jump_map_exceptional;
      if ((jump_map_it = cleanup_jump_map.find(target)) != cleanup_jump_map.end()) {
        builder().br(jump_map_it->second.block, variable_location);
        if (phi_value)
          Tvm::value_cast<Tvm::Phi>(jump_map_it->second.storage)->add_edge(builder().block(), phi_value);
        return;
      } else if (!m_state.cleanup->m_except_only || !target) {
        // Need to run the cleanup
        const SourceLocation& cleanup_loc = m_state.cleanup->m_location;

        // Branch to new block and run cleanup
        Tvm::ValuePtr<Tvm::Block> next_block = m_output->new_block(cleanup_loc, m_state.cleanup->m_dominator);
        builder().br(next_block, variable_location);
        Tvm::ValuePtr<Tvm::Phi> next_phi;
        if (phi_value) {
          next_phi = next_block->insert_phi(phi_value->type(), cleanup_loc);
          next_phi->add_edge(builder().block(), phi_value);
          phi_value = next_phi;
        }

        builder().set_insert_point(next_block);
        m_state.cleanup->run(*this);
        
        TvmJumpData jd;
        jd.block = next_block;
        jd.storage = next_phi ? Tvm::ValuePtr<>(next_phi) : storage;
        cleanup_jump_map.insert(std::make_pair(target, jd));

        // Destroy object
        variable_location = cleanup_loc;
      }
      m_state = m_state.cleanup->m_state;
    }
  }
}

/**
 * \brief Generate a cleanup sequence for normal (rather than exceptional) exit.
 */
void TvmFunctionBuilder::cleanup_to(const TvmCleanupPtr& top) {
  for (; m_state.cleanup != top; m_state = m_state.cleanup->m_state)
    m_state.cleanup->run(*this);
}

TvmResult TvmFunctionBuilder::build(const TreePtr<Term>& term) {
  if (boost::optional<TvmResult> r = m_state.scope->get(term))
    return *r;

  TvmResult value;
  if (term->is_functional() && tree_isa<Functional>(term)) {
    value = tvm_lower_functional(*this, term);
    builder().eval(value.value, term->location());
    if (value.upref)
      builder().eval(value.upref, term->location());
  } else {
    value = build_instruction(term);
  }
  
  if (term->result_type.pure)
    m_state.scope->put(term, value);
  
  return value;
}

/**
 * \brief Build an expression, then destroy any result it may produce.
 */
void TvmFunctionBuilder::build_void(const TreePtr<Term>& term) {
  TvmCleanupPtr cleanup = m_state.cleanup;
  build(term);
  cleanup_to(cleanup);
}

TvmResult TvmFunctionBuilder::build_generic(const TreePtr<GenericType>& generic) {
  return tvm_lower_generic(m_state.scope, *this, generic);
}

TvmFunctionBuilder::DominatorState TvmFunctionBuilder::dominator_state() {
  DominatorState ds;
  ds.block = builder().block();
  ds.state = m_state;
  return ds;
}

bool TvmFunctionBuilder::merge_exit_list_entry_bottom(const MergeExitList::value_type& el) {
  return el.mode == term_mode_bottom;
}

/**
 * \brief Merge different execution contexts into a single context.
 * 
 * This is used for If-Then-Else and jump groups.
 * 
 * \param values List of exit blocks and values from each block to merge into a single execution path.
 * This is modified by this function. Should really be an r-value ref in C++11.
 */
TvmResult TvmFunctionBuilder::merge_exit(const TermResultType& type, MergeExitList& values, const DominatorState& dominator, const SourceLocation& location) {
  // Erase all bottom values
  values.erase(std::remove_if(values.begin(), values.end(), &TvmFunctionBuilder::merge_exit_list_entry_bottom), values.end());
  
  TvmResult result;
  if (values.size() > 1) {
    Tvm::ValuePtr<Tvm::Block> exit_block = m_output->new_block(location, dominator.block);
    
    Tvm::ValuePtr<Tvm::Phi> phi;
    if ((type.mode != term_mode_value) || type.type->is_register_type()) {
      Tvm::ValuePtr<> phi_type = build(type.type).value;
      if (type.mode != term_mode_value)
        phi_type = Tvm::FunctionalBuilder::pointer_type(phi_type, location);

      builder().set_insert_point(exit_block);
      Tvm::ValuePtr<Tvm::Phi> phi = builder().phi(phi_type, location);
    }

    for (MergeExitList::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii) {
      builder().set_insert_point(ii->state.block);
      m_state = ii->state.state;
      
      if (phi) {
        phi->add_edge(ii->state.block, ii->value);
      } else {
        switch (ii->mode) {
        case term_mode_value: break;
        case term_mode_lref: copy_construct(type.type, m_current_result_storage, ii->value, location); break;
        case term_mode_rref: move_construct(type.type, m_current_result_storage, ii->value, location); break;
        default: PSI_FAIL("unknown enum value");
        }
      }
      
      builder().br(exit_block, location);
    }
    builder().set_insert_point(exit_block);
    m_state = dominator.state;
    return TvmResult(m_state.scope.get(), phi ? Tvm::ValuePtr<>(phi) : m_current_result_storage);
  } else if (values.size() == 1) {
    builder().set_insert_point(values.front().state.block);
    if ((type.mode == term_mode_value) && !type.type->is_register_type()) {
      switch (values.front().mode) {
      case term_mode_value: PSI_ASSERT(values.front().value == m_current_result_storage); break;
      case term_mode_lref: copy_construct(type.type, m_current_result_storage, values.front().value, location); break;
      case term_mode_rref: move_construct(type.type, m_current_result_storage, values.front().value, location); break;
      default: PSI_FAIL("unknown enum value");
      }
      m_state = dominator.state;
      return TvmResult(m_state.scope.get(), m_current_result_storage);
    } else {
      m_state = dominator.state;
      return TvmResult(m_state.scope.get(), values.front().value);
    }
  } else {
    return TvmResult::bottom();
  }
}

TvmCleanup::TvmCleanup(bool except_only, const SourceLocation& location)
: m_except_only(except_only), m_location(location) {
}

/**
 * \brief Add a cleanup to the cleanup list.
 */
void TvmFunctionBuilder::push_cleanup(const TvmCleanupPtr& cleanup) {
  cleanup->m_state = m_state;
  cleanup->m_dominator = builder().block();
  m_state.cleanup = cleanup;
}

TvmResult TvmFunctionBuilder::get_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                                 const SourceLocation& location, const TreePtr<Implementation>& maybe_implementation) {
  TreePtr<Implementation> implementation;
  if (!maybe_implementation) {
    PSI_STD::vector<TreePtr<OverloadValue> > scope_extra;
    for (TvmFunctionState::LocalImplementationList::const_iterator ii = m_state.implementation_list.begin(), ie = m_state.implementation_list.end(); ii != ie; ++ii) {
      for (PSI_STD::vector<TreePtr<Implementation> >::const_iterator ji = (*ii)->implementations.begin(), je = (*ii)->implementations.end(); ji != je; ++ji) {
        if ((*ji)->overload_type == interface)
          scope_extra.push_back(*ji);
      }
    }
    implementation = treeptr_cast<Implementation>(overload_lookup(interface, parameters, location, scope_extra));
  } else {
    implementation = maybe_implementation;
  }
  
  if (implementation->dynamic)
    return build(implementation->value);
  
  PSI_NOT_IMPLEMENTED();
}
}
}
