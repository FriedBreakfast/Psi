#include "TvmLowering.hpp"
#include "TermBuilder.hpp"
#include "TopologicalSort.hpp"
#include "PlatformCompile.hpp"

#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Function.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Compiler {
/**
 * \brief Exception raised when building a value globally fails.
 * 
 * This should never escape to the user.
 */    
class TvmNotGlobalException : public std::exception {
public:
  TvmNotGlobalException() throw() {}
  virtual ~TvmNotGlobalException() throw() {}
  virtual const char *what() const throw() {return "Internal error: global value not available at global scope";}
};

TvmScope::TvmScope()
: m_depth(0),
m_in_progress_generic_scope(NULL) {
}

TvmScope::TvmScope(const TvmScopePtr& parent)
: m_parent(parent),
m_depth(parent->m_depth+1),
m_in_progress_generic_scope(NULL) {
}

TvmScopePtr TvmScope::new_root() {
  return boost::make_shared<TvmScope>();
}

TvmScopePtr TvmScope::new_(const TvmScopePtr& parent) {
  return boost::make_shared<TvmScope>(parent);
}

TvmScope* TvmScope::put_scope(TvmScope *given) {
  if (!given) {
    TvmScope *root = this;
    while (root->m_parent)
      root = root->m_parent.get();
    return root;
  } else {
#if PSI_DEBUG
    PSI_ASSERT(given->m_depth <= m_depth);
    TvmScope *parent = this;
    while (parent->m_depth > given->m_depth)
      parent = parent->m_parent.get();
    PSI_ASSERT(parent == given);
#endif
    return given;
  }
}

TvmGenericScope *TvmScope::generic_put_scope(TvmScope *given) {
  TvmScope *scope = this;
  while (!scope->m_in_progress_generic_scope) {
    scope = scope->m_parent.get();
    PSI_ASSERT(scope);
  }
  
#if PSI_DEBUG
  if (given) {
    TvmScope *parent = scope;
    while (parent->m_depth > given->m_depth)
      parent = parent->m_parent.get();
    PSI_ASSERT(parent == given);
  }
#endif
  
  return scope->m_in_progress_generic_scope;
}

/**
 * \brief Return the root ancestor of this scope.
 */
TvmScope* TvmScope::root() {
  TvmScope *scope = this;
  while (scope->depth())
    scope = scope->m_parent.get();
  PSI_ASSERT(scope && !scope->m_parent);
  return scope;
}

boost::optional<TvmResult> TvmScope::get(const TreePtr<Term>& key) {
  for (TvmScope *scope = this; scope; scope = scope->m_parent.get()) {
    if (scope->m_in_progress_generic_scope) {
      TvmGenericScope::VariableMapType::const_iterator it = scope->m_in_progress_generic_scope->variables.find(key);
      if (it != scope->m_in_progress_generic_scope->variables.end())
        return it->second;
    }
    
    VariableMapType::const_iterator it = scope->m_variables.find(key);
    if (it != scope->m_variables.end()) {
      TvmResultScope rs;
      rs.scope = scope;
      return TvmResult(rs, it->second);
    }
  }
  
  return boost::none;
}

void TvmScope::put(const TreePtr<Term>& key, const TvmResult& result) {
  if (result.scope.in_progress_generic) {
    TvmGenericScope *scope = generic_put_scope(result.scope.scope);
    PSI_CHECK(scope->variables.insert(TvmGenericScope::VariableMapType::value_type(key, result)).second);
  } else {
    TvmScope *target = put_scope(result.scope.scope);
    PSI_CHECK(target->m_variables.insert(VariableMapType::value_type(key, result)).second);
  }
}

boost::optional<TvmResult> TvmScope::get_generic(const TreePtr<GenericType>& key) {
  for (TvmScope *scope = this; scope; scope = scope->m_parent.get()) {
    if (scope->m_in_progress_generic_scope) {
      TvmGenericScope::GenericMapType::const_iterator it = scope->m_in_progress_generic_scope->generics.find(key);
      if (it != scope->m_in_progress_generic_scope->generics.end())
        return it->second;
    }
    
    GenericMapType::const_iterator it = scope->m_generics.find(key);
    if (it != scope->m_generics.end()) {
      TvmResultScope rs;
      rs.scope = scope;
      return TvmResult(rs, it->second);
    }
  }
  
  return boost::none;
}

void TvmScope::put_generic(const TreePtr<GenericType>& key, const TvmResult& result) {
  if (result.scope.in_progress_generic) {
    TvmGenericScope *scope = generic_put_scope(result.scope.scope);
    PSI_CHECK(scope->generics.insert(TvmGenericScope::GenericMapType::value_type(key, result)).second);
  } else {
    TvmScope *target = put_scope(result.scope.scope);
    PSI_CHECK(target->m_generics.insert(GenericMapType::value_type(key, result)).second);
  }
}

/**
 * \brief Return the lower of two scopes.
 * 
 * One must be the ancestor of the other.
 */
TvmResultScope TvmScope::join(const TvmResultScope& lhs, const TvmResultScope& rhs) {
  TvmResultScope rs;
  if (!lhs.scope) {
    rs.scope = rhs.scope;
  } else if (!rhs.scope) {
    rs.scope = lhs.scope;
  } else {
#if PSI_DEBUG
    TvmScope *outer;
#endif
    if (lhs.scope->m_depth > rhs.scope->m_depth) {
#if PSI_DEBUG
      outer = rhs.scope;
#endif
      rs.scope = lhs.scope;
    } else {
#if PSI_DEBUG
      outer = lhs.scope;
#endif
      rs.scope = rhs.scope;
    }
    
#if PSI_DEBUG
    TvmScope *sc = rs.scope;
    while (sc->m_depth > outer->m_depth)
      sc = sc->m_parent.get();
    PSI_ASSERT(sc == outer);
#endif
  }
  
  rs.in_progress_generic = lhs.in_progress_generic || rhs.in_progress_generic;
  return rs;
}

TvmFunctionalBuilder::TvmFunctionalBuilder(CompileContext& compile_context, Tvm::Context& tvm_context)
: m_compile_context(&compile_context),
m_tvm_context(&tvm_context) {
}

/**
 * \brief Lower a generic type.
 */
TvmResult tvm_lower_generic(TvmScope& scope, TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic) {
  if (boost::optional<TvmResult> r = scope.get_generic(generic))
    return *r;

  bool is_outer_generic = false;
  TvmGenericScope generic_scope;
  if (!scope.m_in_progress_generic_scope) {
    scope.m_in_progress_generic_scope = &generic_scope;
    is_outer_generic = true;
  }
  
  PSI_STD::vector<TreePtr<Term> > anonymous_list;
  Tvm::RecursiveType::ParameterList parameters;
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = generic->pattern.begin(), ie = generic->pattern.end(); ii != ie; ++ii) {
    // Need to rewrite parameter to anonymous to build lowered type with RecursiveParameter
    // Would've made more seense if I'd built the two systems with a more similar parameter convention.
    TreePtr<Term> rewrite_type = (*ii)->specialize(generic->location(), anonymous_list);
    TreePtr<Anonymous> rewrite_anon = TermBuilder::anonymous(rewrite_type, term_mode_value, rewrite_type->location());
    anonymous_list.push_back(rewrite_anon);
    
    Tvm::ValuePtr<> tvm_type = builder.build(rewrite_type).value;
    Tvm::ValuePtr<Tvm::RecursiveParameter> param = Tvm::RecursiveParameter::create(tvm_type, false, (*ii)->location());
    parameters.push_back(*param);
    scope.put(rewrite_anon, TvmResult(TvmResultScope(NULL, true), param));
  }
  
  Tvm::ValuePtr<Tvm::RecursiveType> recursive = Tvm::RecursiveType::create(builder.tvm_context(), parameters, generic->location());

  // Insert generic into map before building it because it may recursively reference itself.
  TvmResult& generic_slot = scope.m_in_progress_generic_scope->generics[generic];
  generic_slot = TvmResult(TvmResultScope(NULL, true), recursive);

  TreePtr<Term> inner_type = generic->member_type()->specialize(generic->location(), anonymous_list);
  TvmResult inner = builder.build(inner_type);
  recursive->resolve(inner.value);
  
  generic_slot = TvmResult(TvmResultScope(inner.scope.scope, true), recursive);
  
  if (is_outer_generic) {
    PSI_ASSERT(scope.m_in_progress_generic_scope == &generic_scope);
    scope.m_in_progress_generic_scope = NULL;
#if PSI_DEBUG
    TvmScope *result_scope = generic_scope.generics.empty() ? static_cast<TvmScope*>(NULL) : generic_scope.generics.begin()->second.scope.scope;
#endif
    for (TvmGenericScope::GenericMapType::const_iterator ii = generic_scope.generics.begin(), ie = generic_scope.generics.end(); ii != ie; ++ii) {
      TvmResult r = ii->second;
      PSI_ASSERT(r.scope.scope == result_scope);
      r.scope.in_progress_generic = false;
      scope.put_generic(ii->first, r);
    }
    
    boost::optional<TvmResult> r = scope.get_generic(generic);
    PSI_ASSERT(r);
    return *r;
  } else {
    return TvmResult(inner.scope, recursive);
  }
}

TvmTargetScope::TvmTargetScope(CompileContext& compile_context, Tvm::Context& tvm_context, const PropertyValue& target)
: m_jit_target(NULL), m_compile_context(&compile_context), m_tvm_context(&tvm_context), m_target(target) {
  m_scope = TvmScope::new_root();
}

TvmTargetScope::TvmTargetScope(TvmTargetScope& jit_target, const PropertyValue& target)
: m_jit_target(&jit_target), m_compile_context(jit_target.m_compile_context), m_tvm_context(jit_target.m_tvm_context), m_target(target) {
  m_scope = TvmScope::new_root();
}

/**
 * TvmFunctionalBuilder specialization used to build the type of global variables.
 */
class TvmTypeBuilder : public TvmFunctionalBuilder {
  TvmScope *m_scope;
  
public:
  TvmTypeBuilder(CompileContext& compile_context, Tvm::Context& tvm_context, TvmScope *root_scope)
  : TvmFunctionalBuilder(compile_context, tvm_context) {
    m_scope = root_scope;
  }
  
  virtual TvmResult build(const TreePtr<Term>& term) {
    if (boost::optional<TvmResult> r = m_scope->get(term))
      return *r;
    
    if (term->type && !term->type->is_primitive_type())
      throw TvmNotGlobalException();

    TvmResult value;
    if (tree_isa<Functional>(term) || tree_isa<Global>(term)) {
      value = tvm_lower_functional(*this, term);
    } else {
      PSI_ASSERT_MSG(!tree_isa<Global>(term), "Global terms should not be built here");
      throw TvmNotGlobalException();
    }
    
    if (term->pure)
      m_scope->put(term, value);
    
    return value;
  }
  
  virtual TvmResult build_generic(const TreePtr<GenericType>& generic) {
    return tvm_lower_generic(*m_scope, *this, generic);
  }
  
  virtual TvmResult build_global(const TreePtr<Global>&) {
    throw TvmNotGlobalException();
  }
  
  virtual TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>&) {
    throw TvmNotGlobalException();
  }
  
  virtual TvmResult build_implementation(const TreePtr<Interface>& PSI_UNUSED(interface), const PSI_STD::vector<TreePtr<Term> >& PSI_UNUSED(parameters),
                                         const SourceLocation& PSI_UNUSED(location), const TreePtr<Implementation>& PSI_UNUSED(maybe_implementation)) {
    throw TvmNotGlobalException();
  }
  
  virtual TvmResult load(const Tvm::ValuePtr<>& PSI_UNUSED(ptr), const SourceLocation& PSI_UNUSED(location)) {
    throw TvmNotGlobalException();
  }
};

/**
 * \brief Builds the type of a global variable.
 */
TvmResult TvmTargetScope::build_type(const TreePtr<Term>& value, const SourceLocation& location) {
  try {
    return TvmTypeBuilder(compile_context(), tvm_context(), m_scope->root()).build(value);
  } catch (TvmNotGlobalException&) {
    compile_context().error_throw(location, "Type of a global is not global itself");
  }
}

/**
 * \brief Evaluate a target callback on this target.
 */
PropertyValue TvmTargetScope::evaluate_callback(const TreePtr<TargetCallback>& callback) {
  return callback->evaluate(m_target, m_jit_target ? m_jit_target->m_target : m_target);
}

const TvmTargetSymbol& TvmTargetScope::library_symbol(const TreePtr<LibrarySymbol>& lib_global) {
  TvmTargetSymbol& sym = m_library_symbols[lib_global];
  if (sym.type)
    return sym;

  PropertyValue symbol = evaluate_callback(lib_global->callback);
  if (symbol.type() != PropertyValue::t_map)
    compile_context().error_throw(lib_global->location(), "Global symbol identifiers are expected to have map type");
  PropertyMap& symbol_map = symbol.map();
  PropertyMap::const_iterator type_it = symbol_map.find("type");
  if (type_it == symbol_map.end())
    compile_context().error_throw(lib_global->location(), "Global symbol property map is missing property 'type'");
  if (type_it->second == "c") {
    PropertyMap::const_iterator name_it = symbol_map.find("name");
    if (name_it == symbol_map.end())
      compile_context().error_throw(lib_global->location(), "Global symbol property map is missing property 'name'");
    if (name_it->second.type() != PropertyValue::t_str)
      compile_context().error_throw(lib_global->location(), "Global symbol property map entry 'name' is not a string");
    sym.name = name_it->second.str();
  } else {
    compile_context().error_throw(lib_global->location(), "Unrecognised symbol type");
  }
  
  sym.type = build_type(lib_global->type, lib_global->location()).value;
  sym.linkage = Tvm::link_import;
  return sym;
}

TvmObjectCompilerBase::TvmObjectCompilerBase(TvmJitCompiler *jit_compiler, TvmTargetScope *target, const TreePtr<Module>& module, Tvm::Module *tvm_module)
: m_jit_compiler(jit_compiler), m_target(target), m_module(module), m_tvm_module(tvm_module) {
  m_scope = TvmScope::new_(m_target->scope());
}

void TvmObjectCompilerBase::reset_tvm_module(Tvm::Module *module) {
  m_tvm_module = module;
}

/**
 * Returns
 * \code TvmResult(scope(), get_global_bare(global)) \endcode
 */
TvmResult TvmObjectCompilerBase::get_global(const TreePtr<Global>& global) {
  return TvmResult(scope(), get_global_bare(global));
}

namespace {
  Tvm::Linkage tvm_linkage(Linkage linkage, bool same_module) {
    switch (linkage) {
    case link_local: return Tvm::link_local;
    case link_private: return Tvm::link_private;
    case link_one_definition: return Tvm::link_one_definition;
    case link_public: return same_module ? Tvm::link_export : Tvm::link_import;
    case link_none: PSI_FAIL("Globals with no linkage should not generate a symbol");
    default: PSI_FAIL("Unknown linkage type");
    }
  }
}

/**
 * \brief Get a global variable in this module.
 * 
 * This function is not responsible for compiling the global variable. This just creates a global
 * of the correct name, type and linkage with no body.
 */
Tvm::ValuePtr<Tvm::Global> TvmObjectCompilerBase::get_global_bare(const TreePtr<Global>& global) {
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    const std::string& symbol_name = m_symbol_names.symbol_name(mod_global);
    if (Tvm::ValuePtr<Tvm::Global> existing = m_tvm_module->get_member(symbol_name)) {
      notify_existing_global(global, existing);
      return existing;
    }
    
    if (TreePtr<GlobalStatement> stmt = dyn_treeptr_cast<GlobalStatement>(mod_global))
      if (stmt->mode != statement_mode_value)
        compile_context().error_throw(stmt->location(), "Global statements which are not of value-type do not translate directly to TVM, use build_global_statement");
    
    if (m_module == mod_global->module) {
      TvmResult type = target().build_type(global->type, mod_global->location());
      Tvm::ValuePtr<Tvm::Global> lowered = m_tvm_module->new_member(symbol_name, type.value, global->location());
      lowered->set_linkage(tvm_linkage(mod_global->linkage, true));
      return lowered;
    } else {
      if (mod_global->linkage != link_public)
        compile_context().error_throw(global->location(), "Module private global variable used in a different module");
      
      TvmResult type = target().build_type(global->type, mod_global->location());
      Tvm::ValuePtr<Tvm::Global> lowered = m_tvm_module->new_member(symbol_name, type.value, global->location());
      lowered->set_linkage(Tvm::link_import);
      notify_global(mod_global, lowered);
      return lowered;
    }
  } else if (TreePtr<LibrarySymbol> lib_sym = dyn_treeptr_cast<LibrarySymbol>(global)) {
    const TvmTargetSymbol& symbol = target().library_symbol(lib_sym);
    Tvm::ValuePtr<Tvm::Global> result = m_tvm_module->new_member(symbol.name, symbol.type, lib_sym->location());
    result->set_linkage(symbol.linkage);
    notify_library_symbol(lib_sym, result);
    return result;
  } else {
    PSI_FAIL("Unrecognised global subclass");
  }
}

/**
 * \brief Create a global variable for a FunctionalEvaluate 
 */
TvmResult TvmObjectCompilerBase::get_global_evaluate(const TreePtr<GlobalEvaluate>& evaluate) {
  PSI_NOT_IMPLEMENTED();
}

/**
 * \brief Check if a particular implementation has already been instantiated.
 * 
 * Returns TvmResult::bottom() if no matching implementation is found.
 */
TvmResult tvm_check_implementation(const TvmGeneratedImplementationSet& implementations, const TreePtr<Interface>& interface,
                                   const PSI_STD::vector<TreePtr<Term> >& parameters, std::set<TreePtr<ModuleGlobal> >& dependencies) {
  // Check for existing copy
  const SharedList<TvmGeneratedImplementation> *interface_impls = implementations.lookup(interface);
  if (!interface_impls)
    return TvmResult::bottom();
  
  for (SharedList<TvmGeneratedImplementation>::const_iterator ii = interface_impls->begin(), ie = interface_impls->end(); ii != ie; ++ii) {
    PSI_ASSERT(parameters.size() == ii->parameters.size());
    for (std::size_t ji = 0, je = parameters.size(); ji != je; ++ji) {
      if (!ii->parameters[ji]->convert_match(parameters[ji]))
        goto no_match;
    }
    // Successful match
    dependencies.insert(ii->dependencies.begin(), ii->dependencies.end());
    return ii->result;

  no_match:;
  }
  
  return TvmResult::bottom();
}

/**
 * \brief Get an existing implementation if available.
 */
TvmResult TvmObjectCompilerBase::check_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, std::set<TreePtr<ModuleGlobal> >& dependencies) {
  return tvm_check_implementation(m_implementations, interface, parameters, dependencies);
}

/**
 * TvmFunctionalBuilder specialization used to build constant global variables.
 */
class TvmGlobalBuilder : public TvmFunctionalBuilder {
  TvmObjectCompilerBase *m_self;
  std::set<TreePtr<ModuleGlobal> > *m_dependencies;

public:
  TvmGlobalBuilder(TvmObjectCompilerBase *self, std::set<TreePtr<ModuleGlobal> >& dependencies)
  : TvmFunctionalBuilder(self->compile_context(), self->tvm_context()),
  m_self(self),
  m_dependencies(&dependencies) {
  }
    
  virtual TvmResult build(const TreePtr<Term>& term) {
    if (boost::optional<TvmResult> r = m_self->scope()->get(term))
      return *r;

    if (term->type && !term->type->is_primitive_type())
      throw TvmNotGlobalException();

    TvmResult value;
    if (tree_isa<Functional>(term) || tree_isa<Global>(term)) {
      value = tvm_lower_functional(*this, term);
    } else {
      throw TvmNotGlobalException();
    }
    
    if (term->pure)
      m_self->scope()->put(term, value);
    
    return value;
  }
    
  virtual TvmResult build_generic(const TreePtr<GenericType>& generic) {
      return tvm_lower_generic(*m_self->scope(), *this, generic);
    }

  virtual TvmResult build_global(const TreePtr<Global>& global) {
    if (TreePtr<ModuleGlobal> mg = dyn_treeptr_cast<ModuleGlobal>(global))
      m_dependencies->insert(mg);
    return m_self->get_global(global);
  }
    
  virtual TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>& global) {
    m_dependencies->insert(global);
    return m_self->get_global_evaluate(global);
  }
  
  virtual TvmResult build_implementation(const TreePtr<Interface>& interface, const std::vector<TreePtr<Term> >& parameters,
                                         const SourceLocation& location, const TreePtr<Implementation>& maybe_implementation) {
    return m_self->get_implementation(interface, parameters, *m_dependencies, location, maybe_implementation);
  }
  
  virtual TvmResult load(const Tvm::ValuePtr<>& PSI_UNUSED(ptr), const SourceLocation& PSI_UNUSED(location)) {
    throw TvmNotGlobalException();
  }
};

/**
 * \brief Get an implementation in a global variable.
 */
TvmResult TvmObjectCompilerBase::get_implementation(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters,
                                                    std::set<TreePtr<ModuleGlobal> >& dependencies, const SourceLocation& location,
                                                    const TreePtr<Implementation>& maybe_implementation) {
  TreePtr<Implementation> implementation;
  PSI_STD::vector<TreePtr<Term> > wildcards;
  if (!maybe_implementation) {
    OverloadLookupResult lookup = overload_lookup(interface, parameters, location, default_);
    implementation = treeptr_cast<Implementation>(lookup.value);
    wildcards.swap(lookup.wildcards);
  } else {
    implementation = maybe_implementation;
    wildcards = overload_match(maybe_implementation, parameters, location);
  }
  
  PSI_ASSERT(!implementation->dynamic);
  TreePtr<Term> value = implementation->value->specialize(location, wildcards);
  PSI_ASSERT(value->is_functional());
  std::set<TreePtr<ModuleGlobal> > my_dependencies;
  TvmResult tvm_value = TvmGlobalBuilder(this, my_dependencies).build(value);
  std::string symbol_name = symbol_implementation_name(interface, parameters);
  Tvm::ValuePtr<Tvm::GlobalVariable> gvar = m_tvm_module->new_global_variable_set(symbol_name, tvm_value.value, location);
  gvar->set_linkage(Tvm::link_one_definition);
  gvar->set_constant(true);
  gvar->set_merge(true);
  
  Tvm::ValuePtr<> ptr = gvar;
  for (PSI_STD::vector<int>::const_iterator ii = implementation->path.begin(), ie = implementation->path.end(); ii != ie; ++ii)
    ptr = Tvm::FunctionalBuilder::element_ptr(ptr, *ii, location);
  
  TvmResult expected_type = TvmGlobalBuilder(this, my_dependencies).build(interface->type_after(parameters, location));
  if (Tvm::isa<Tvm::Exists>(expected_type.value))
    ptr = Tvm::FunctionalBuilder::introduce_exists(expected_type.value, ptr, location);

  TvmResult result = TvmResult(m_scope, ptr);
  
  TvmGeneratedImplementation gen_impl;
  gen_impl.parameters = parameters;
  gen_impl.result = result;
  gen_impl.dependencies = my_dependencies;
  m_implementations.put(interface, m_implementations.get_default(interface).extend(gen_impl));
  
  dependencies.insert(my_dependencies.begin(), my_dependencies.end());
  return result;
}

void TvmObjectCompilerBase::run_module_global(const TreePtr<ModuleGlobal>& global, TvmGlobalStatus& status) {
  if (!status.lowered)
    status.lowered = get_global_bare(global);
  
  if (TreePtr<Function> function = dyn_treeptr_cast<Function>(global)) {
    Tvm::ValuePtr<Tvm::Function> tvm_func = Tvm::value_cast<Tvm::Function>(status.lowered);
    tvm_lower_function(*this, function, tvm_func, status.dependencies);
  } else if (TreePtr<GlobalVariable> global_var = dyn_treeptr_cast<GlobalVariable>(global)) {
    Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = Tvm::value_cast<Tvm::GlobalVariable>(status.lowered);
    // This is the only global property which is independent of whether the global is constant-initialized or not
    TvmResult value;
    try {
      value = TvmGlobalBuilder(this, status.dependencies).build(global_var->value());
      
      tvm_gvar->set_value(value.value);
      tvm_gvar->set_constant(global_var->constant);
      tvm_gvar->set_merge(global_var->merge);
    } catch (TvmNotGlobalException&) {
      tvm_gvar->set_value(Tvm::FunctionalBuilder::undef(tvm_gvar->value_type(), global_var->location()));
      std::string ctor_name = m_symbol_names.unique_name("_Y_ctor");
      Tvm::ValuePtr<Tvm::Function> constructor = m_tvm_module->new_constructor(ctor_name, global_var->location());
      TreePtr<Term> ctor_tree = TermBuilder::initialize_value(global_var, global_var->value(), TermBuilder::empty_value(compile_context()), global_var->location());
      tvm_lower_init(*this, global_var->module, ctor_tree, constructor, status.dependencies);
      status.init = constructor;
      
      if (global_var->type && (global_var->type->type_info().type_mode == type_mode_complex)) {
        std::string dtor_name = m_symbol_names.unique_name("_Y_dtor");
        Tvm::ValuePtr<Tvm::Function> destructor = m_tvm_module->new_constructor(dtor_name, global_var->location());
        TreePtr<Term> dtor_body = TermBuilder::finalize_value(global_var, global_var->location());
        tvm_lower_init(*this, global_var->module, dtor_body, destructor, status.dependencies);
        status.fini = destructor;
      }
    }
  } else if (TreePtr<GlobalStatement> global_stmt = dyn_treeptr_cast<GlobalStatement>(global)) {
    // Only global variable-like statements are built here
    PSI_ASSERT(global_stmt->mode == statement_mode_value);
    Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = Tvm::value_cast<Tvm::GlobalVariable>(status.lowered);
    TvmResult value;
    try {
      value = TvmGlobalBuilder(this, status.dependencies).build(global_stmt->value);
      tvm_gvar->set_value(value.value);
    } catch (TvmNotGlobalException&) {
      tvm_gvar->set_value(Tvm::FunctionalBuilder::undef(tvm_gvar->value_type(), global_stmt->location()));
      std::string ctor_name = m_symbol_names.unique_name("_Y_ctor");
      Tvm::ValuePtr<Tvm::Function> constructor = m_tvm_module->new_constructor(ctor_name, global_stmt->location());
      TreePtr<Term> ctor_tree = TermBuilder::initialize_value(global_stmt, global_stmt->value, TermBuilder::empty_value(compile_context()), global_stmt->location());
      tvm_lower_init(*this, global_stmt->module, ctor_tree, constructor, status.dependencies);
      status.init = constructor;
      
      if (global_stmt->type && (global_stmt->type->type_info().type_mode == type_mode_complex)) {
        std::string dtor_name = m_symbol_names.unique_name("_Y_dtor");
        Tvm::ValuePtr<Tvm::Function> destructor = m_tvm_module->new_constructor(dtor_name, global_stmt->location());
        TreePtr<Term> dtor_body = TermBuilder::finalize_value(global_stmt, global_stmt->location());
        tvm_lower_init(*this, global_stmt->module, dtor_body, destructor, status.dependencies);
        status.fini = destructor;
      }
    }
  } else {
    PSI_FAIL("Unknown module global type");
  }
}

TvmJitObjectCompiler::TvmJitObjectCompiler(TvmJitCompiler *jit_compiler, TvmTargetScope *target, const TreePtr<Module>& module)
: TvmObjectCompilerBase(jit_compiler, target, module, NULL) {
}

void TvmJitObjectCompiler::notify_existing_global(const TreePtr<Global>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global) {
  Tvm::ValuePtr<Tvm::Global> previous;
  
  if (TreePtr<ModuleGlobal> module_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    TvmJitCompiler::BuiltGlobalMap& globals = jit_compiler().m_built_globals;
    TvmJitCompiler::BuiltGlobalMap::iterator it = globals.find(module_global);
    if (it == globals.end())
      compile_context().error_throw(global->location(), boost::format("Conflicting global symbol name: %s") % tvm_global->name());
    previous = it->second.lowered;
  } else if (TreePtr<LibrarySymbol> lib_sym = dyn_treeptr_cast<LibrarySymbol>(global)) {
    TvmJitCompiler::LibrarySymbolMap& symbols = jit_compiler().m_library_symbols;
    TvmJitCompiler::LibrarySymbolMap::iterator it = symbols.find(lib_sym);
    if (it == symbols.end())
      it = symbols.insert(std::make_pair(lib_sym, tvm_global)).first;
    PSI_ASSERT(it != symbols.end());
    previous = it->second;
  } else {
    PSI_FAIL("Unknown global type");
  }
  
  if (previous && (previous->type() != tvm_global->type()))
    global->compile_context().error_throw(global->location(), boost::format("Conflicting global symbol: %s") % tvm_global->name());
}

void TvmJitObjectCompiler::notify_global(const TreePtr<ModuleGlobal>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global) {
  TvmGlobalStatus& status = jit_compiler().m_built_globals[global];
  if (!status.lowered)
    status.lowered = tvm_global;
}

void TvmJitObjectCompiler::notify_external_global(const TreePtr<ModuleGlobal>& global, const Tvm::ValuePtr<Tvm::Global>& tvm_global) {
  // Need to load external library??
  PSI_NOT_IMPLEMENTED();
}

void TvmJitObjectCompiler::notify_library_symbol(const TreePtr<LibrarySymbol>& lib_sym, const Tvm::ValuePtr<Tvm::Global>& tvm_global) {
  jit_compiler().load_library(lib_sym->library);
  Tvm::ValuePtr<Tvm::Global>& common = jit_compiler().m_library_symbols[lib_sym];
  if (!common)
    common = tvm_global;
}

TvmJitCompiler::TvmJitCompiler(TvmTargetScope& target, const PropertyValue& jit_configuration)
: m_target(&target) {
  boost::shared_ptr<Tvm::JitFactory> factory =
    Tvm::JitFactory::get_specific(target.compile_context().error_context().bind(SourceLocation::root_location("(jit)")), jit_configuration);
  m_jit = factory->create_jit();
}

/**
 * \brief Get the TvmJitObjectCompiler used for a given module, or create on if it does not exist.
 * 
 * This also ensures that the module compiler has a non-NULL TVM module associated with it,
 * since these are cleared by the JIT when moving objects to the JIT backend.
 */
TvmJitObjectCompiler& TvmJitCompiler::module_compiler(const TreePtr<Module>& module) {
  boost::shared_ptr<TvmJitObjectCompiler>& ptr = m_modules[module];
  if (!ptr)
    ptr = boost::make_shared<TvmJitObjectCompiler>(this, m_target, module);
  if (!ptr->tvm_module()) {
    boost::shared_ptr<Tvm::Module> tvm_module = boost::make_shared<Tvm::Module>(&m_target->tvm_context(), module->name, module->location());
    m_current_modules.push_back(std::make_pair(ptr.get(), tvm_module));
    ptr->reset_tvm_module(tvm_module.get());
  }
  return *ptr;
}

/**
 * \brief Figure out which globals that require initialization this global depends on.
 * 
 * \param already_built Include all globals which have already been fully built.
 */
std::set<TreePtr<ModuleGlobal> > TvmJitCompiler::initializer_dependencies(const TreePtr<ModuleGlobal>& global, bool already_built) {
  std::set<TreePtr<ModuleGlobal> > dependencies;
  
  std::vector<TreePtr<ModuleGlobal> > queue;
  queue.push_back(global);
  
  while (!queue.empty()) {
    TvmGlobalStatus& q_status = m_built_globals[queue.back()];
    queue.pop_back();
      
    std::set<TreePtr<ModuleGlobal> > dependency_visited;
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = q_status.dependencies.begin(), je = q_status.dependencies.end(); ji != je; ++ji) {
      TvmGlobalStatus& j_status = m_built_globals[*ji];
      if (j_status.status == TvmGlobalStatus::global_built) {
        if (j_status.init)
          dependencies.insert(*ji);
        else if (dependency_visited.insert(*ji).second)
          queue.push_back(*ji);
      } else if (already_built) {
        PSI_ASSERT(j_status.status == TvmGlobalStatus::global_built_all);
        dependencies.insert(*ji);
      }
    }
  }
  
  return dependencies;
}

/**
 * \brief Compile a symbol to TVM.
 * 
 * This handles dependencies between globals, which are tracked by building a dependency
 * set for each global built and then traversing the set, rather than recursing.
 */
Tvm::ValuePtr<Tvm::Global> TvmJitCompiler::build_module_global(const TreePtr<ModuleGlobal>& global) {
  std::vector<TreePtr<ModuleGlobal> > queue;
  std::set<TreePtr<ModuleGlobal> > visited;
  queue.push_back(global);
  visited.insert(global);
  
  CompileContext& compile_context = m_target->compile_context();
  
  while (!queue.empty()) {
    TreePtr<ModuleGlobal> current = queue.back();
    queue.pop_back();
    PSI_ASSERT(current->module == global->module);
    TvmGlobalStatus& status = m_built_globals[current];
    
    switch (status.status) {
    case TvmGlobalStatus::global_built:
    case TvmGlobalStatus::global_built_all:
      break;
    
    case TvmGlobalStatus::global_in_progress:
      compile_context.error_throw(current->location(), "Circular dependency amongst global variables");
      
    case TvmGlobalStatus::global_ready: {
      status.status = TvmGlobalStatus::global_in_progress;
      module_compiler(current->module).run_module_global(current, status);
      status.status = TvmGlobalStatus::global_built;
      break;
    }
    
    default: PSI_FAIL("Unrecognised global status value");
    }
    
    if (status.status == TvmGlobalStatus::global_built_all)
      continue;
    
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ii = status.dependencies.begin(), ie = status.dependencies.end(); ii != ie; ++ii) {
      if (visited.insert(*ii).second)
        queue.push_back(*ii);
    }
  }


  // Topologically sort all elements so we can figure out initialization/finalization priorities
  std::vector<TreePtr<ModuleGlobal> > sorted;
  std::multimap<TreePtr<ModuleGlobal>, TreePtr<ModuleGlobal> > dependencies;
  for (std::set<TreePtr<ModuleGlobal> >::const_iterator ii = visited.begin(), ie = visited.end(); ii != ie; ++ii) {
    TvmGlobalStatus& status = m_built_globals[*ii];
    if ((status.status != TvmGlobalStatus::global_built) || !status.init)
      continue;
    
    sorted.push_back(*ii);
    std::set<TreePtr<ModuleGlobal> > init_deps = initializer_dependencies(*ii, false);
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = init_deps.begin(), je = init_deps.end(); ji != je; ++ji)
      dependencies.insert(std::make_pair(*ji, *ii));
  }
  
  try {
    topological_sort(sorted.begin(), sorted.end(), dependencies);
  } catch (std::logic_error&) {
    compile_context.error_throw(global->location(), "Circular dependency found in dependents of global");
  }
  
  for (std::vector<TreePtr<ModuleGlobal> >::const_iterator ii = sorted.begin(), ie = sorted.end(); ii != ie; ++ii) {
    unsigned priority = 0;
    std::set<TreePtr<ModuleGlobal> > init_deps = initializer_dependencies(*ii, true);
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = init_deps.begin(), je = init_deps.end(); ji != je; ++ji)
      priority = std::max(priority, m_built_globals[*ji].priority + 1);
    
    TvmJitObjectCompiler& module = module_compiler((*ii)->module);
    TvmGlobalStatus& status = m_built_globals[*ii];
    status.status = TvmGlobalStatus::global_built_all;
    status.priority = priority;
    
    PSI_ASSERT(status.init);
    module.tvm_module()->constructors().push_back(std::make_pair(status.init, priority));
    if (status.fini)
      module.tvm_module()->destructors().push_back(std::make_pair(status.fini, priority));
  }

  for (std::set<TreePtr<ModuleGlobal> >::const_iterator ii = visited.begin(), ie = visited.end(); ii != ie; ++ii) {
    TvmGlobalStatus& status = m_built_globals[*ii];
    if (status.status != TvmGlobalStatus::global_built)
      continue;
    
    unsigned priority = 0;
    std::set<TreePtr<ModuleGlobal> > init_deps = initializer_dependencies(*ii, false);
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = init_deps.begin(), je = init_deps.end(); ji != je; ++ji)
      priority = std::max(priority, m_built_globals[*ji].priority);
    
    status.status = TvmGlobalStatus::global_built_all;
    status.priority = priority;
  }
  
  TvmGlobalStatus& result_status = m_built_globals[global];
  PSI_ASSERT(result_status.status == TvmGlobalStatus::global_built_all);
  return result_status.lowered;
}

/**
 * \brief Build a library symbol.
 * 
 * The symbol is created in a special "library module" held by the JIT. This mechanism is not used to fetch
 * external symbols for use in symbols built by the JIT, it exists so that the user can get symbols by the
 * same lookup mechanism.
 */
Tvm::ValuePtr<Tvm::Global> TvmJitCompiler::build_library_symbol(const TreePtr<LibrarySymbol>& lib_sym) {
  LibrarySymbolMap::iterator lib_sym_it = m_library_symbols.find(lib_sym);
  if (lib_sym_it != m_library_symbols.end())
    return lib_sym_it->second;
  
  load_library(lib_sym->library);
  
  const TvmTargetSymbol& sym = m_target->library_symbol(lib_sym);
  if (!m_library_module)
    m_library_module = boost::make_shared<Tvm::Module>(&m_target->tvm_context(), "(lib)", lib_sym->location());
  
  Tvm::ValuePtr<Tvm::Global> result;
  if (result = m_library_module->get_member(sym.name)) {
    if (result->type() != sym.type)
      m_target->compile_context().error_throw(lib_sym->location(), "Conflicting types for external global");
  } else {
    result = m_library_module->new_member(sym.name, sym.type, lib_sym->location());
    result->set_linkage(sym.linkage);
  }
  
  m_library_symbols.insert(std::make_pair(lib_sym, result));
  return result;
}

void TvmJitCompiler::load_library(const TreePtr<Library>& library) {
  boost::shared_ptr<Platform::PlatformLibrary>& tvm_lib = m_libraries[library];
  if (!tvm_lib) {
    PropertyValue pv = m_target->evaluate_callback(library->callback);
    tvm_lib = Platform::load_module(pv);
  }
}

/**
 * \brief Just-in-time compile a symbol.
 */
void* TvmJitCompiler::compile(const TreePtr<Global>& global) {
  Tvm::ValuePtr<Tvm::Global> built;
  if (TreePtr<ModuleGlobal> module_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    built = build_module_global(module_global);
  } else if (TreePtr<LibrarySymbol> lib_sym = dyn_treeptr_cast<LibrarySymbol>(global)) {
    built = build_library_symbol(lib_sym);
  } else {
    m_target->compile_context().error_throw(global->location(), "Cannot build global: unknown tree type");
  }
  
  // Ensure all modules are up to date in the JIT
  while (!m_current_modules.empty()) {
    CurrentModuleList::value_type& val = m_current_modules.back();
    val.first->reset_tvm_module(NULL);
    m_built_modules.push_back(val.second);
    m_jit->add_module(val.second.get());
    m_current_modules.pop_back();
  }
  
  if (m_library_module) {
    m_built_modules.push_back(m_library_module);
    m_jit->add_module(m_library_module.get());
    m_library_module.reset();
  }
  
  return m_jit->get_symbol(built);
}

const PropertyValue& TvmJit::target_configuration(CompileErrorPair& err_loc, const PropertyValue& configuration) {
  boost::optional<std::string> jit_key = configuration.path_str("jit_target");
  if (!jit_key)
    err_loc.error_throw("Configuration property 'jit_target' specified");
  const PropertyValue *target = configuration.path_value_ptr("targets." + *jit_key);
  if (!target)
    err_loc.error_throw(boost::format("JIT target '%s' (specified by 'jit_target' property) does not exist") % *jit_key);
  return *target;
}

const PropertyValue& TvmJit::jit_configuration(CompileErrorPair& err_loc, const PropertyValue& configuration) {
  const PropertyValue& target = target_configuration(err_loc, configuration);
  boost::optional<std::string> tvm_key = target.path_str("tvm");
  if (!tvm_key)
    err_loc.error_throw("JIT target missing 'tvm' property");
  const PropertyValue *config = configuration.path_value_ptr("tvm." + *tvm_key);
  if (!config)
    err_loc.error_throw(boost::format("TVM configuration '%s' used by JIT does not exist") % *tvm_key);
  return *config;
}

TvmJit::TvmJit(CompileContext& compile_context, CompileErrorPair& err_loc, const PropertyValue& configuration)
: m_tvm_context(&compile_context.error_context()),
m_target_scope(compile_context, m_tvm_context, target_configuration(err_loc, configuration)),
m_jit_compiler(m_target_scope, jit_configuration(err_loc, configuration)) {
}
}
}
