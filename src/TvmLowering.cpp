#include "TvmLowering.hpp"
#include "TermBuilder.hpp"
#include "TopologicalSort.hpp"

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

TvmScopePtr TvmScope::root() {
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
#ifdef PSI_DEBUG
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
  
#ifdef PSI_DEBUG
  if (given) {
    TvmScope *parent = scope;
    while (parent->m_depth > given->m_depth)
      parent = parent->m_parent.get();
    PSI_ASSERT(parent == given);
  }
#endif
  
  return scope->m_in_progress_generic_scope;
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
    PSI_CHECK(scope->variables.insert(std::make_pair<TreePtr<Term>, TvmResult>(key, result)).second);
  } else {
    TvmScope *target = put_scope(result.scope.scope);
    PSI_CHECK(target->m_variables.insert(std::make_pair<TreePtr<Term>, TvmResultBase>(key, result)).second);
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
    PSI_CHECK(scope->generics.insert(std::make_pair<TreePtr<GenericType>, TvmResult>(key, result)).second);
  } else {
    TvmScope *target = put_scope(result.scope.scope);
    PSI_CHECK(target->m_generics.insert(std::make_pair<TreePtr<GenericType>, TvmResultBase>(key, result)).second);
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
#ifdef PSI_DEBUG
    TvmScope *outer;
#endif
    if (lhs.scope->m_depth > rhs.scope->m_depth) {
#ifdef PSI_DEBUG
      outer = rhs.scope;
#endif
      rs.scope = lhs.scope;
    } else {
#ifdef PSI_DEBUG
      outer = lhs.scope;
#endif
      rs.scope = rhs.scope;
    }
    
#ifdef PSI_DEBUG
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

TvmCompiler::TvmCompiler(CompileContext *compile_context)
: m_compile_context(compile_context),
m_root_scope(TvmScope::root()) {
  boost::shared_ptr<Tvm::JitFactory> factory = Tvm::JitFactory::get("llvm");
  m_jit = factory->create_jit();
  m_library_module.reset(new Tvm::Module(&m_tvm_context, "(library)", SourceLocation::root_location("(library)")));
}

TvmCompiler::~TvmCompiler() {
}

std::string TvmCompiler::mangle_name(const LogicalSourceLocationPtr& location) {
  std::ostringstream ss;
  ss << "_Y";
  std::vector<LogicalSourceLocationPtr> ancestors;
  for (LogicalSourceLocationPtr ptr = location; ptr->parent(); ptr = ptr->parent())
    ancestors.push_back(ptr);
  for (std::vector<LogicalSourceLocationPtr>::reverse_iterator ii = ancestors.rbegin(), ie = ancestors.rend(); ii != ie; ++ii) {
    const String& name = (*ii)->name();
    ss << name.length() << '_' << name;
  }
  return ss.str();
}

TvmCompiler::TvmModule& TvmCompiler::get_module(const TreePtr<Module>& module) {
  TvmModule& tvm_module = m_modules[module];
  if (!tvm_module.module) {
    tvm_module.jit_current = false;
    tvm_module.n_constructors = 0;
    tvm_module.scope = TvmScope::new_(m_root_scope);
    tvm_module.module.reset(new Tvm::Module(&m_tvm_context, module->name, module->location()));
  }
  return tvm_module;
}

TvmCompiler::TvmPlatformLibrary& TvmCompiler::get_platform_library(const TreePtr<Library>& lib) {
  TvmPlatformLibrary& tvm_lib = m_libraries[lib];
  if (!tvm_lib.library) {
    PropertyValue pv = lib->callback->evaluate(m_local_target, m_local_target);
    tvm_lib.library = Platform::load_library(pv);
  }
  
  return tvm_lib;
}

/**
 * \brief Build global in a general module for the user.
 * 
 * This also recursively builds any dependencies.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build_global(const TreePtr<Global>& global) {
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    return Tvm::value_cast<Tvm::Global>(build_module_global(mod_global));
  } else if (TreePtr<LibrarySymbol> lib_sym = dyn_treeptr_cast<LibrarySymbol>(global)) {
    return run_library_symbol(lib_sym);
  } else {
    PSI_FAIL("Unrecognised global subclass");
  }
}

/**
 * \brief Get the built status of a global variable.
 */
TvmCompiler::GlobalStatus& TvmCompiler::get_global_status(const TreePtr<ModuleGlobal>& global) {
  return get_module(global->module).symbols[global];
}

/**
 * \brief Figure out which globals that require initialization this global depends on.
 * 
 * \param already_built Include all globals which have already been fully built.
 */
std::set<TreePtr<ModuleGlobal> > TvmCompiler::initializer_dependencies(const TreePtr<ModuleGlobal>& global, bool already_built) {
  std::set<TreePtr<ModuleGlobal> > dependencies;
  
  std::vector<TreePtr<ModuleGlobal> > queue;
  queue.push_back(global);
  
  while (!queue.empty()) {
    GlobalStatus& q_status = get_global_status(queue.back());
    queue.pop_back();
      
    std::set<TreePtr<ModuleGlobal> > dependency_visited;
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = q_status.dependencies.begin(), je = q_status.dependencies.end(); ji != je; ++ji) {
      GlobalStatus& j_status = get_global_status(*ji);
      if (j_status.status == global_built) {
        if (j_status.init)
          dependencies.insert(*ji);
        else if (dependency_visited.insert(*ji).second)
          queue.push_back(*ji);
      } else if (already_built) {
        PSI_ASSERT(j_status.status == global_built_all);
        dependencies.insert(*ji);
      }
    }
  }
  
  return dependencies;
}

/**
 * \brief Ensure a global and all of its dependencies have been generated.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build_module_global(const TreePtr<ModuleGlobal>& global) {
  std::vector<TreePtr<ModuleGlobal> > queue;
  std::set<TreePtr<ModuleGlobal> > visited;
  queue.push_back(global);
  visited.insert(global);
  
  while (!queue.empty()) {
    TreePtr<ModuleGlobal> current = queue.back();
    queue.pop_back();
    PSI_ASSERT(current->module == global->module);
    GlobalStatus& status = get_global_status(current);
    
    switch (status.status) {
    case global_built:
    case global_built_all:
      break;
    
    case global_in_progress:
      compile_context().error_throw(current.location(), "Circular dependency amongst global variables");
      
    case global_ready: {
      status.status = global_in_progress;
      run_module_global(current);
      status.status = global_built;
      break;
    }
    
    default: PSI_FAIL("Unrecognised global status value");
    }
    
    if (status.status == global_built_all)
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
    GlobalStatus& status = get_global_status(*ii);
    if ((status.status != global_built) || !status.init)
      continue;
    
    sorted.push_back(*ii);
    std::set<TreePtr<ModuleGlobal> > init_deps = initializer_dependencies(*ii, false);
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = init_deps.begin(), je = init_deps.end(); ji != je; ++ji)
      dependencies.insert(std::make_pair(*ji, *ii));
  }
  
  try {
    topological_sort(sorted.begin(), sorted.end(), dependencies);
  } catch (std::logic_error&) {
    compile_context().error_throw(global->location(), "Circular dependency found in dependents of global");
  }
  
  for (std::vector<TreePtr<ModuleGlobal> >::const_iterator ii = sorted.begin(), ie = sorted.end(); ii != ie; ++ii) {
    unsigned priority = 0;
    std::set<TreePtr<ModuleGlobal> > init_deps = initializer_dependencies(*ii, true);
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = init_deps.begin(), je = init_deps.end(); ji != je; ++ji)
      priority = std::max(priority, get_global_status(*ji).priority + 1);
    
    TvmModule& tvm_module = get_module((*ii)->module);
    GlobalStatus& status = tvm_module.symbols[*ii];
    status.status = global_built_all;
    status.priority = priority;
    
    PSI_ASSERT(status.init);
    tvm_module.module->constructors().push_back(std::make_pair(status.init, priority));
    if (status.fini)
      tvm_module.module->destructors().push_back(std::make_pair(status.fini, priority));
  }

  for (std::set<TreePtr<ModuleGlobal> >::const_iterator ii = visited.begin(), ie = visited.end(); ii != ie; ++ii) {
    GlobalStatus& status = get_global_status(*ii);
    if (status.status != global_built)
      continue;
    
    unsigned priority = 0;
    std::set<TreePtr<ModuleGlobal> > init_deps = initializer_dependencies(*ii, false);
    for (std::set<TreePtr<ModuleGlobal> >::const_iterator ji = init_deps.begin(), je = init_deps.end(); ji != je; ++ji)
      priority = std::max(priority, get_global_status(*ji).priority);
    
    status.status = global_built_all;
    status.priority = priority;
  }
  
  GlobalStatus& result_status = get_global_status(global);
  PSI_ASSERT(result_status.status == global_built_all);
  return result_status.lowered;
}

/**
 * \brief Create a global variable for a FunctionalEvaluate 
 */
TvmResult TvmCompiler::get_global_evaluate(const TreePtr<GlobalEvaluate>& evaluate, const TreePtr<Module>& module) {
  PSI_NOT_IMPLEMENTED();
}

/**
 * \brief Build global in a specific module.
 * 
 * This create an external reference to symbols in another module when required.
 * Note that this merely creates the symbol for the relevant global; it does not
 * ensure that the global in question has been compiled.
 */
TvmResult TvmCompiler::get_global(const TreePtr<Global>& global, const TreePtr<Module>& module) {
  const TvmScopePtr& scope = module_scope(module);
  
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    if (TreePtr<GlobalStatement> stmt = dyn_treeptr_cast<GlobalStatement>(mod_global))
      if (stmt->mode != statement_mode_value)
        compile_context().error_throw(stmt.location(), "Global statements which are not of value-type do not translate directly to TVM, use build_global_statement");
    
    TvmModule& tvm_module = get_module(mod_global->module);
    if (module == mod_global->module) {
      GlobalStatus& status = tvm_module.symbols[mod_global];
      if (!status.lowered) {
        TvmResult type = build_type(global->type, mod_global->location());
        std::string name = mangle_name(global->location().logical);
        status.lowered = tvm_module.module->new_member(name, type.value, global->location());
      }
      return TvmResult(scope, status.lowered);
    } else {
      ModuleExternalGlobalMap::iterator external_it = tvm_module.external_symbols.find(mod_global);
      if (external_it != tvm_module.external_symbols.end())
        return TvmResult(scope, external_it->second);

      if (mod_global->local)
        compile_context().error_throw(global.location(), "Module-global global variable used in a different module");
      
      TvmResult type = build_type(global->type, mod_global->location());
      std::string name = mangle_name(global->location().logical);
      Tvm::ValuePtr<Tvm::Global> lowered = tvm_module.module->new_member(name, type.value, global->location());
      PSI_CHECK(tvm_module.external_symbols.insert(std::make_pair(mod_global, lowered)).second);
      return TvmResult(scope, lowered);
    }
  } else if (TreePtr<LibrarySymbol> lib_sym = dyn_treeptr_cast<LibrarySymbol>(global)) {
    TvmModule& tvm_module = get_module(module);
    
    ModuleLibrarySymbolMap::iterator lib_it = tvm_module.library_symbols.find(lib_sym);
    if (lib_it != tvm_module.library_symbols.end())
      return TvmResult(scope, lib_it->second);
    
    Tvm::ValuePtr<Tvm::Global> native = run_library_symbol(lib_sym);
    Tvm::ValuePtr<Tvm::Global> result = tvm_module.module->new_member(native->name(), native->value_type(), native->location());
    PSI_CHECK(tvm_module.library_symbols.insert(std::make_pair(lib_sym, result)).second);
    return TvmResult(scope, result);
  } else {
    PSI_FAIL("Unrecognised global subclass");
  }
}

/**
 * \brief Build an external symbol.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::run_library_symbol(const TreePtr<LibrarySymbol>& lib_global) {
  TvmPlatformLibrary& lib = get_platform_library(lib_global->library);
  TvmLibrarySymbol& sym = lib.symbol_info[lib_global];
  if (sym.value)
    return sym.value;

  PropertyValue symbol = lib_global->callback->evaluate(m_local_target, m_local_target);
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
  
  if (Tvm::ValuePtr<Tvm::Global> existing = m_library_module->get_member(sym.name)) {
    sym.value = existing;
    return existing;
  }
  
  TvmResult type = build_type(lib_global->type, lib_global->location());
  if (Tvm::ValuePtr<Tvm::FunctionType> ftype = Tvm::dyn_cast<Tvm::FunctionType>(type.value)) {
    sym.value = m_library_module->new_function(sym.name, ftype, lib_global->location());
  } else {
    sym.value = m_library_module->new_global_variable(sym.name, type.value, lib_global->location());
  }
  return sym.value;
}

/**
 * TvmFunctionalBuilder specialization used to build constant global variables.
 */
class TvmGlobalBuilder : public TvmFunctionalBuilder {
  TvmCompiler *m_self;
  TreePtr<Module> m_module;
  TvmScopePtr m_scope;
  std::set<TreePtr<ModuleGlobal> > *m_dependencies;

public:
  TvmGlobalBuilder(TvmCompiler *self, const TreePtr<Module>& module, std::set<TreePtr<ModuleGlobal> >& dependencies)
  : TvmFunctionalBuilder(self->compile_context(), self->tvm_context()),
  m_self(self),
  m_module(module),
  m_dependencies(&dependencies) {
    m_scope = m_self->module_scope(module);
  }
    
  virtual TvmResult build(const TreePtr<Term>& term) {
    if (boost::optional<TvmResult> r = m_scope->get(term))
      return *r;

    TvmResult value;
    if (tree_isa<Functional>(term) || tree_isa<Global>(term)) {
      value = tvm_lower_functional(*this, term);
    } else {
      throw TvmNotGlobalException();
    }
    
    if (term->pure)
      m_scope->put(term, value);
    
    return value;
  }
    
  virtual TvmResult build_generic(const TreePtr<GenericType>& generic) {
      return tvm_lower_generic(m_scope, *this, generic);
    }

  virtual TvmResult build_global(const TreePtr<Global>& global) {
    if (TreePtr<ModuleGlobal> mg = dyn_treeptr_cast<ModuleGlobal>(global))
      m_dependencies->insert(mg);
    return m_self->get_global(global, m_module);
  }
    
  virtual TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>& global) {
    m_dependencies->insert(global);
    return m_self->get_global_evaluate(global, m_module);
  }
};

void TvmCompiler::run_module_global(const TreePtr<ModuleGlobal>& global) {
  TvmModule& tvm_module = get_module(global->module);
  
  // Ensure that the global variable has been created
  get_global(global, global->module);
  
  ModuleGlobalMap::iterator status_it = tvm_module.symbols.find(global);
  PSI_ASSERT(status_it != tvm_module.symbols.end());
  GlobalStatus& status = status_it->second;
  PSI_ASSERT(status.lowered && (status.status == global_in_progress));
  
  if (TreePtr<Function> function = dyn_treeptr_cast<Function>(global)) {
    Tvm::ValuePtr<Tvm::Function> tvm_func = Tvm::value_cast<Tvm::Function>(status.lowered);
    tvm_func->set_private(function->local);
    tvm_lower_function(*this, function, tvm_func, status.dependencies);
  } else if (TreePtr<GlobalVariable> global_var = dyn_treeptr_cast<GlobalVariable>(global)) {
    Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = Tvm::value_cast<Tvm::GlobalVariable>(status.lowered);
    // This is the only global property which is independent of whether the global is constant-initialized or not
    tvm_gvar->set_private(global_var->local);
    TvmResult value;
    try {
      value = TvmGlobalBuilder(this, global_var->module, status.dependencies).build(global_var->value());
      
      tvm_gvar->set_value(value.value);
      tvm_gvar->set_constant(global_var->constant);
      tvm_gvar->set_merge(global_var->merge);
    } catch (TvmNotGlobalException&) {
      unsigned ctor_idx = tvm_module.n_constructors++;
      std::string ctor_name = str(boost::format("_Y_ctor%d") % ctor_idx);
      Tvm::ValuePtr<Tvm::Function> constructor = tvm_module.module->new_constructor(ctor_name, global_var->location());
      TreePtr<Term> gv_ptr = TermBuilder::ptr_to(global_var, global_var->location());
      TreePtr<Term> ctor_tree = TermBuilder::initialize_ptr(gv_ptr, global_var->value(), TermBuilder::empty_value(compile_context()), global_var->location());
      tvm_lower_init(*this, global_var->module, ctor_tree, constructor, status.dependencies);
      status.init = constructor;
      
      if (global_var->type && (global_var->type->type_info().type_mode == type_mode_complex)) {
        std::string dtor_name = str(boost::format("_Y_dtor%d") % ctor_idx);
        Tvm::ValuePtr<Tvm::Function> destructor = tvm_module.module->new_constructor(dtor_name, global_var->location());
        TreePtr<Term> dtor_body = TermBuilder::finalize_ptr(gv_ptr, global_var->location());
        tvm_lower_init(*this, global_var->module, dtor_body, destructor, status.dependencies);
        status.fini = destructor;
      }
    }
  } else if (TreePtr<GlobalStatement> global_stmt = dyn_treeptr_cast<GlobalStatement>(global)) {
    // Only global variable-like statements are built here
    PSI_ASSERT(global_stmt->mode == statement_mode_value);
    Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = Tvm::value_cast<Tvm::GlobalVariable>(status.lowered);
    tvm_gvar->set_private(global_stmt->local);
    TvmResult value;
    try {
      value = TvmGlobalBuilder(this, global_stmt->module, status.dependencies).build(global_var->value());
      tvm_gvar->set_value(value.value);
    } catch (TvmNotGlobalException&) {
      unsigned ctor_idx = tvm_module.n_constructors++;
      std::string ctor_name = str(boost::format("_Y_ctor%d") % ctor_idx);
      Tvm::ValuePtr<Tvm::Function> constructor = tvm_module.module->new_constructor(ctor_name, global_stmt->location());
      TreePtr<Term> gv_ptr = TermBuilder::ptr_to(global_stmt, global_stmt->location());
      TreePtr<Term> ctor_tree = TermBuilder::initialize_ptr(gv_ptr, global_stmt->value, TermBuilder::empty_value(compile_context()), global_stmt->location());
      tvm_lower_init(*this, global_stmt->module, ctor_tree, constructor, status.dependencies);
      status.init = constructor;
      
      if (global_var->type && (global_var->type->type_info().type_mode == type_mode_complex)) {
        std::string dtor_name = str(boost::format("_Y_dtor%d") % ctor_idx);
        Tvm::ValuePtr<Tvm::Function> destructor = tvm_module.module->new_constructor(dtor_name, global_stmt->location());
        TreePtr<Term> dtor_body = TermBuilder::finalize_ptr(gv_ptr, global_var->location());
        tvm_lower_init(*this, global_stmt->module, dtor_body, destructor, status.dependencies);
        status.fini = destructor;
      }
    }
  } else {
    PSI_FAIL("Unknown module global type");
  }
}

/**
 * \brief Just-in-time compile a symbol.
 */
void* TvmCompiler::jit_compile(const TreePtr<Global>& global) {
  Tvm::ValuePtr<Tvm::Global> built = build_global(global);
  
  // Ensure all modules are up to date in the JIT
  for (ModuleMap::iterator ii = m_modules.begin(), ie = m_modules.end(); ii != ie; ++ii) {
    if (!ii->second.jit_current) {
      m_jit->add_or_rebuild_module(ii->second.module.get(), true);
      ii->second.jit_current = true;
    }
  }
  
  return m_jit->get_symbol(built);
}

/**
 * TvmFunctionalBuilder specialization used to build the type of global variables.
 */
class TvmTypeBuilder : public TvmFunctionalBuilder {
  TvmScopePtr m_scope;
  
public:
  TvmTypeBuilder(TvmCompiler *self)
  : TvmFunctionalBuilder(self->compile_context(), self->tvm_context()) {
    m_scope = self->root_scope();
  }
  
  virtual TvmResult build(const TreePtr<Term>& term) {
    if (boost::optional<TvmResult> r = m_scope->get(term))
      return *r;

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
    return tvm_lower_generic(m_scope, *this, generic);
  }
  
  virtual TvmResult build_global(const TreePtr<Global>&) {
    throw TvmNotGlobalException();
  }
  
  virtual TvmResult build_global_evaluate(const TreePtr<GlobalEvaluate>&) {
    throw TvmNotGlobalException();
  }
};

/**
 * \brief Builds the type of a global variable.
 */
TvmResult TvmCompiler::build_type(const TreePtr<Term>& value, const SourceLocation& location) {
  try {
    return TvmTypeBuilder(this).build(value);
  } catch (TvmNotGlobalException&) {
    compile_context().error_throw(location, "Type of a global is not global itself");
  }
}

/**
 * \brief Lower a generic type.
 */
TvmResult tvm_lower_generic(const TvmScopePtr& scope, TvmFunctionalBuilder& builder, const TreePtr<GenericType>& generic) {
  if (boost::optional<TvmResult> r = scope->get_generic(generic))
    return *r;

  bool is_outer_generic = false;
  TvmGenericScope generic_scope;
  if (!scope->m_in_progress_generic_scope) {
    scope->m_in_progress_generic_scope = &generic_scope;
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
    Tvm::ValuePtr<Tvm::RecursiveParameter> param = Tvm::RecursiveParameter::create(tvm_type, false, ii->location());
    parameters.push_back(*param);
    scope->put(rewrite_anon, TvmResult(TvmResultScope(NULL, true), param));
  }
  
  Tvm::ValuePtr<Tvm::RecursiveType> recursive = Tvm::RecursiveType::create(builder.tvm_context(), parameters, generic.location());

  // Insert generic into map before building it because it may recursively reference itself.
  TvmResult& generic_slot = scope->m_in_progress_generic_scope->generics[generic];
  generic_slot = TvmResult(TvmResultScope(NULL, true), recursive);

  TreePtr<Term> inner_type = generic->member_type()->specialize(generic->location(), anonymous_list);
  TvmResult inner = builder.build(inner_type);
  recursive->resolve(inner.value);
  
  generic_slot = TvmResult(TvmResultScope(inner.scope.scope, true), recursive);
  
  if (is_outer_generic) {
    PSI_ASSERT(scope->m_in_progress_generic_scope == &generic_scope);
    scope->m_in_progress_generic_scope = NULL;
#ifdef PSI_DEBUG
    TvmScope *result_scope = generic_scope.generics.empty() ? static_cast<TvmScope*>(NULL) : generic_scope.generics.begin()->second.scope.scope;
#endif
    for (TvmGenericScope::GenericMapType::const_iterator ii = generic_scope.generics.begin(), ie = generic_scope.generics.end(); ii != ie; ++ii) {
      TvmResult r = ii->second;
      PSI_ASSERT(r.scope.scope == result_scope);
      r.scope.in_progress_generic = false;
      scope->put_generic(ii->first, r);
    }
    
    boost::optional<TvmResult> r = scope->get_generic(generic);
    PSI_ASSERT(r);
    return *r;
  } else {
    return TvmResult(inner.scope, recursive);
  }
}
}
}
