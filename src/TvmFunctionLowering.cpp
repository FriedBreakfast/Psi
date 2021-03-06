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
StackFreeCleanup::StackFreeCleanup(const Tvm::ValuePtr<>& stack_alloc, const SourceLocation& location)
: TvmCleanup(false, location), m_stack_alloc(stack_alloc) {}

void StackFreeCleanup::run(TvmFunctionBuilder& builder) const {
  builder.builder().freea(m_stack_alloc, location());
}

DestroyCleanup::DestroyCleanup(const Tvm::ValuePtr<>& slot, const TreePtr<Term>& type, const SourceLocation& location)
: TvmCleanup(false, location), m_slot(slot), m_type(type) {}
  
void DestroyCleanup::run(TvmFunctionBuilder& builder) const {
  builder.object_destroy(m_slot, m_type, location());
}

TvmFunctionBuilder::TvmFunctionBuilder(TvmObjectCompilerBase& tvm_compiler, const TreePtr<Module>& module, std::set<TreePtr<ModuleGlobal> >& dependencies)
: TvmFunctionalBuilder(tvm_compiler.compile_context(), tvm_compiler.tvm_context()),
m_tvm_compiler(&tvm_compiler),
m_module(module),
m_dependencies(&dependencies) {
}

void TvmFunctionBuilder::run_function(const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output) {
  m_state.scope = TvmScope::new_(m_tvm_compiler->scope());
  m_output = output;
  
  TreePtr<FunctionType> ftype = treeptr_cast<FunctionType>(function->type);
  
  if (ftype->result_mode == result_mode_by_value)
    m_return_storage = output->parameters().back();

  const SourceLocation& location = function->location();

  // We need this to be non-NULL
  m_return_target = function->return_target;
  if (!m_return_target)
    m_return_target = TermBuilder::exit_target(ftype->result_type, ftype->result_mode, location);
  
  // Can be less due to sret parameters
  PSI_ASSERT(function->arguments.size() + ftype->interfaces.size() <= output->parameters().size());
  Tvm::Function::ParameterList::iterator arg_tvm_ii = output->parameters().begin();
  for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator arg_ii = function->arguments.begin(), arg_ie = function->arguments.end(); arg_ii != arg_ie; ++arg_ii, ++arg_tvm_ii)
    m_state.scope->put(*arg_ii, TvmResult(m_state.scope, *arg_tvm_ii));
  
  PSI_STD::vector<TreePtr<Term> > term_arguments = vector_from<TreePtr<Term> >(function->arguments);

  PSI_STD::vector<TreePtr<Implementation> > impl;
  for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator if_ii = ftype->interfaces.begin(), if_ie = ftype->interfaces.end(); if_ii != if_ie; ++if_ii, ++arg_tvm_ii) {
    const TreePtr<InterfaceValue>& iv = *if_ii;
    TreePtr<Anonymous> interface_value = TermBuilder::anonymous(iv->type->specialize(location, term_arguments), term_mode_value, location);
    m_state.scope->put(interface_value, TvmResult(m_state.scope, *arg_tvm_ii));
    
    PSI_STD::vector<TreePtr<Term> > parameters;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ji = iv->parameters.begin(), je = iv->parameters.end(); ji != je; ++ji)
      parameters.push_back((*ji)->specialize(location, term_arguments));
    
    impl.push_back(Implementation::new_(iv->interface, OverloadPattern(0, parameters), default_,
                                        ImplementationValue(interface_value, true), location));
  }
  
  TreePtr<Term> body = function->body();
  body = TermBuilder::jump_to(m_return_target, body, location);
  if (!impl.empty())
    body = TermBuilder::introduce_implementation(impl, body, location);
  
  m_builder.set_insert_point(output->new_block(location));
  build(body);
}

void TvmFunctionBuilder::run_init(const TreePtr<Term>& body, const Tvm::ValuePtr<Tvm::Function>& output) {
  m_state.scope = TvmScope::new_(m_tvm_compiler->scope());
  m_output = output;
  
  const SourceLocation& location = body->location();
  // We need this to be non-NULL
  m_return_target = TermBuilder::exit_target(TermBuilder::empty_type(compile_context()), result_mode_functional, location);
  m_builder.set_insert_point(output->new_block(location));
  build(TermBuilder::jump_to(m_return_target, body, location));
}

/**
 * \brief Lower a function.
 * 
 * Constructs a TvmFunctionLowering object and runs it.
 */
void tvm_lower_function(TvmObjectCompilerBase& tvm_compiler, const TreePtr<Function>& function, const Tvm::ValuePtr<Tvm::Function>& output, std::set<TreePtr<ModuleGlobal> >& dependencies) {
  TvmFunctionBuilder(tvm_compiler, function->module, dependencies).run_function(function, output);
}

/**
 * \brief Lower an initialization of finalization function.
 * 
 * This takes a tree for the function body rather than a function tree
 * because the function always has the same type, and this avoids creating
 * spurious entries in the Module.
 */
void tvm_lower_init(TvmObjectCompilerBase& tvm_compiler, const TreePtr<Module>& module, const TreePtr<Term>& body, const Tvm::ValuePtr<Tvm::Function>& output, std::set<TreePtr<ModuleGlobal> >& dependencies) {
  TvmFunctionBuilder(tvm_compiler, module, dependencies).run_init(body, output);
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
    
    if ((jump_map_it = m_state.jump_map.find(target)) != m_state.jump_map.end()) {
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
  while (m_state.cleanup != top) {
    /*
     * Pop state before running cleanup in case cleanup generates a new
     * state (which would lead to infinte recursion)
     */
    TvmCleanupPtr cleanup = m_state.cleanup;
    m_state = m_state.cleanup->m_state;
    if (!cleanup->m_except_only)
      cleanup->run(*this);
  }
}

TvmResult TvmFunctionBuilder::build(const TreePtr<Term>& term) {
  if (boost::optional<TvmResult> r = m_state.scope->get(term)) {
    PSI_ASSERT((term->result_info().mode == term_mode_bottom) == !r->value);
    return *r;
  }
  
  TvmResult value;
  if (tree_isa<Functional>(term) || tree_isa<Global>(term)) {
    value = tvm_lower_functional(*this, term);
  } else {
    value = build_instruction(term);
  }
  PSI_ASSERT((term->result_info().mode == term_mode_bottom) == !value.value);
  
  if (term->pure)
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

/**
 * \brief Force a term to functional mode, i.e. not a reference.
 */
TvmResult TvmFunctionBuilder::build_functional(const TreePtr<Term>& term, const SourceLocation& location) {
  PSI_ASSERT(term->type->is_register_type());
  
  switch (term->mode) {
  case term_mode_lref:
  case term_mode_rref: {
    TvmResult inner = build(term);
    PSI_ASSERT(!inner.is_bottom());
    return load(inner.value, location);
  }
    
  case term_mode_bottom:
    return TvmResult::bottom();
    
  case term_mode_value:
    // Argument must always have functional type, so should always be stored functionally
    return build(term);
    
  default: PSI_FAIL("Unexpected term mode");
  }
}

TvmResult TvmFunctionBuilder::build_generic(const TreePtr<GenericType>& generic) {
  return tvm_lower_generic(*m_state.scope, *this, generic);
}

TvmResult TvmFunctionBuilder::build_global(const TreePtr<Global>& global) {
  if (TreePtr<ModuleGlobal> mg = dyn_treeptr_cast<ModuleGlobal>(global))
    m_dependencies->insert(mg);
  return m_tvm_compiler->get_global(global);
}

TvmResult TvmFunctionBuilder::build_global_evaluate(const TreePtr<GlobalEvaluate>& global) {
  m_dependencies->insert(global);
  return m_tvm_compiler->get_global_evaluate(global);
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
TvmResult TvmFunctionBuilder::merge_exit(const TreePtr<Term>& type, TermMode mode, MergeExitList& values, const DominatorState& dominator, const SourceLocation& location) {
  // Erase all bottom values
  values.erase(std::remove_if(values.begin(), values.end(), &TvmFunctionBuilder::merge_exit_list_entry_bottom), values.end());
  
  TvmResult result;
  if (values.size() > 1) {
    Tvm::ValuePtr<Tvm::Block> exit_block = m_output->new_block(location, dominator.block);
    
    Tvm::ValuePtr<Tvm::Phi> phi;
    if ((mode != term_mode_value) || type->is_register_type()) {
      Tvm::ValuePtr<> phi_type = build(type).value;
      if (mode != term_mode_value)
        phi_type = Tvm::FunctionalBuilder::pointer_type(phi_type, location);

      builder().set_insert_point(exit_block);
      Tvm::ValuePtr<Tvm::Phi> phi = builder().phi(phi_type, location);
    }

    for (MergeExitList::const_iterator ii = values.begin(), ie = values.end(); ii != ie; ++ii) {
      PSI_ASSERT(!ii->value.scope.in_progress_generic);
      builder().set_insert_point(ii->state.block);
      m_state = ii->state.state;
      
      if (phi) {
        phi->add_edge(ii->state.block, ii->value.value);
      } else {
        switch (ii->mode) {
        case term_mode_value: break;
        case term_mode_lref: copy_construct(type, m_current_result_storage, ii->value.value, location); break;
        case term_mode_rref: move_construct(type, m_current_result_storage, ii->value.value, location); break;
        default: PSI_FAIL("unknown enum value");
        }
      }
      
      builder().br(exit_block, location);
    }
    builder().set_insert_point(exit_block);
    m_state = dominator.state;
    return TvmResult(m_state.scope, phi ? Tvm::ValuePtr<>(phi) : m_current_result_storage);
  } else if (values.size() == 1) {
    builder().set_insert_point(values.front().state.block);
    if ((mode == term_mode_value) && !type->is_register_type()) {
      switch (values.front().mode) {
      case term_mode_value: PSI_ASSERT(values.front().value.value == m_current_result_storage); break;
      case term_mode_lref: copy_construct(type, m_current_result_storage, values.front().value.value, location); break;
      case term_mode_rref: move_construct(type, m_current_result_storage, values.front().value.value, location); break;
      default: PSI_FAIL("unknown enum value");
      }
      PSI_ASSERT(!values.front().value.scope.in_progress_generic);
      m_state = dominator.state;
      return TvmResult(m_state.scope, m_current_result_storage);
    } else {
      m_state = dominator.state;
      return values.front().value;
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

/**
 * \todo Add parent implementations to global implementation list so e.g. a copy constructor does not generate an extra
 * interface instantiation for the corresponding destructor.
 */
TvmResult TvmFunctionBuilder::build_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                                   const SourceLocation& location, const TreePtr<Implementation>& maybe_implementation) {
  TvmResult result;

  // Check for existing implementation
  result = m_tvm_compiler->check_implementation(interface, parameters, *m_dependencies);
  if (!result.is_bottom())
    return result;
  result = tvm_check_implementation(m_state.generated_implementations, interface, parameters, *m_dependencies);
  if (!result.is_bottom())
    return result;
  
  TreePtr<Implementation> implementation;
  PSI_STD::vector<TreePtr<Term> > wildcards;
  if (!maybe_implementation) {
    PSI_STD::vector<TreePtr<OverloadValue> > scope_extra;
    for (TvmFunctionState::LocalImplementationList::const_iterator ii = m_state.implementation_list.begin(), ie = m_state.implementation_list.end(); ii != ie; ++ii) {
      for (PSI_STD::vector<TreePtr<Implementation> >::const_iterator ji = (*ii)->implementations.begin(), je = (*ii)->implementations.end(); ji != je; ++ji) {
        if ((*ji)->overload_type == interface)
          scope_extra.push_back(*ji);
      }
    }
    OverloadLookupResult lookup = overload_lookup(interface, parameters, location, scope_extra);
    implementation = treeptr_cast<Implementation>(lookup.value);
    wildcards.swap(lookup.wildcards);
  } else {
    implementation = maybe_implementation;
    wildcards = overload_match(maybe_implementation, parameters, location);
  }
  
  const ImplementationValue& impl_value = implementation->implementation_value();
  if (impl_value.dynamic) {
    result = build(impl_value.value);
  } else {
    TreePtr<Term> value = impl_value.value->specialize(location, wildcards);
    PSI_ASSERT(value->is_functional());
    TvmResult tvm_value = build(value);
    if (tvm_value.scope.scope->depth() <= TvmScope::depth_global) {
      return m_tvm_compiler->get_implementation(interface, parameters, *m_dependencies, location, implementation);
    } else {
      Tvm::ValuePtr<> ptr = builder().alloca_const(tvm_value.value, location);
      push_cleanup(boost::make_shared<StackFreeCleanup>(ptr, location));
      for (PSI_STD::vector<unsigned>::const_iterator ii = impl_value.path.begin(), ie = impl_value.path.end(); ii != ie; ++ii)
        ptr = Tvm::FunctionalBuilder::element_ptr(ptr, *ii, location);
      
      TvmResult expected_type = build(interface->type_after(parameters, location));
      if (Tvm::isa<Tvm::Exists>(expected_type.value))
        ptr = Tvm::FunctionalBuilder::introduce_exists(expected_type.value, ptr, location);

      result = TvmResult(m_state.scope, ptr);
    }
  }
  
  TvmGeneratedImplementation gen_impl;
  gen_impl.parameters = parameters;
  gen_impl.result = result;
  m_state.generated_implementations.put(interface, m_state.generated_implementations.get_default(interface).extend(gen_impl));
  
  return result;
}

TvmResult TvmFunctionBuilder::load(const Tvm::ValuePtr<>& ptr, const SourceLocation& location) {
  Tvm::ValuePtr<> val = builder().load(ptr, location);
  return TvmResult(m_state.scope, val);
}
}
}
