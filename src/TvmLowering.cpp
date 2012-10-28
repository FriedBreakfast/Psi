#include "TvmLowering.hpp"

#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Function.hpp"
#include "Tvm/Recursive.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Compiler {
TvmCompiler::TvmCompiler(CompileContext *compile_context)
: m_compile_context(compile_context),
m_functional_builder(compile_context, &m_tvm_context, this) {
  boost::shared_ptr<Tvm::JitFactory> factory = Tvm::JitFactory::get("llvm");
  m_jit = factory->create_jit();
  m_library_module.reset(new Tvm::Module(&m_tvm_context, "(library)", SourceLocation::root_location("(library)")));
}

TvmCompiler::~TvmCompiler() {
}

TvmResult TvmCompiler::build_hook(const TreePtr<Term>& value) {
  if (TreePtr<Global> global = dyn_treeptr_cast<Global>(value))
    return TvmResult::in_register(value->type, tvm_storage_lvalue_ref, build_global(global));
  
  compile_context().error_throw(value->location(), "Value is required in a global context but is not a global value.");
}

TvmResult TvmCompiler::build_define_hook(const TreePtr<GlobalDefine>& define) {
  return m_functional_builder.build(define->value);
}

TvmGenericResult TvmCompiler::build_generic_hook(const TreePtr<GenericType>& generic) {
  return build_generic(generic);
}

Tvm::ValuePtr<> TvmCompiler::load_hook(const Tvm::ValuePtr<>& PSI_UNUSED(ptr), const SourceLocation& PSI_UNUSED(location)) {
  PSI_FAIL("Cannot create global load instruction");
}

/**
 * \brief Build a global or constant value.
 */
TvmResult Psi::Compiler::TvmCompiler::build(const TreePtr<Term>& value) {
  return m_functional_builder.build(value);
}

std::string TvmCompiler::mangle_name(const LogicalSourceLocationPtr& location) {
  std::ostringstream ss;
  ss << "_Y";
  std::vector<LogicalSourceLocationPtr> ancestors;
  for (LogicalSourceLocationPtr ptr = location; ptr->parent(); ptr = ptr->parent())
    ancestors.push_back(ptr);
  for (std::vector<LogicalSourceLocationPtr>::reverse_iterator ii = ancestors.rbegin(), ie = ancestors.rend(); ii != ie; ++ii) {
    const String& name = (*ii)->name();
    ss << name.length() << name;
  }
  return ss.str();
}

TvmCompiler::TvmModule& TvmCompiler::get_module(const TreePtr<Module>& module) {
  TvmModule& tvm_module = m_modules[module];
  if (!tvm_module.module) {
    tvm_module.jit_current = false;
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
 * \brief Build global in a specific module.
 * 
 * This create an external reference to symbols in another module when required.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build_global_in(const TreePtr<Global>& global, const TreePtr<Module>& module) {  
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    if (mod_global->module == module)
      return build_global(mod_global);
    
    TvmModule& tvm_module = get_module(module);

    ModuleGlobalMap::iterator global_it = tvm_module.symbols.find(mod_global);
    if (global_it != tvm_module.symbols.end())
      return global_it->second;
    
    Tvm::ValuePtr<Tvm::Global> native = build_global(mod_global);
    Tvm::ValuePtr<Tvm::Global> result = tvm_module.module->new_member(native->name(), native->value_type(), native->location());
    
    tvm_module.symbols.insert(std::make_pair(mod_global, result));
    return result;
  } else if (TreePtr<LibrarySymbol> lib_sym = dyn_treeptr_cast<LibrarySymbol>(global)) {
    TvmModule& tvm_module = get_module(module);

    ModuleLibrarySymbolMap::iterator global_it = tvm_module.library_symbols.find(lib_sym);
    if (global_it != tvm_module.library_symbols.end())
      return global_it->second;
    
    Tvm::ValuePtr<Tvm::Global> native = build_global(lib_sym);
    Tvm::ValuePtr<Tvm::Global> result = tvm_module.module->new_member(native->name(), native->value_type(), native->location());
    
    tvm_module.library_symbols.insert(std::make_pair(lib_sym, result));
    return result;
  } else {
    return build_global(global);
  }
}

/**
 * \brief Create a Tvm::Global from a Global.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build_global(const TreePtr<Global>& global) {
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    if (TreePtr<ExternalGlobal> ext_global = dyn_treeptr_cast<ExternalGlobal>(global)) {
      PSI_NOT_IMPLEMENTED();
    } else {
      return build_module_global(mod_global);
    }
  } else if (TreePtr<LibrarySymbol> lib_global = dyn_treeptr_cast<LibrarySymbol>(global)) {
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
    
    TvmResult type = m_functional_builder.build_type(lib_global->type);
    if (Tvm::ValuePtr<Tvm::FunctionType> ftype = Tvm::dyn_cast<Tvm::FunctionType>(type.value())) {
      sym.value = m_library_module->new_function(sym.name, ftype, lib_global->location());
    } else {
      sym.value = m_library_module->new_global_variable(sym.name, type.value(), lib_global->location());
    }
    return sym.value;
  } else {
    PSI_FAIL("Unknown global type");
  }
}

/**
 * \brief Build a module global.
 * 
 * If this global depends on other globals, this function will recursively search for those and build them.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build_module_global(const TreePtr<ModuleGlobal>& global) {
  // Check if this global is already built
  TvmModule& global_module = get_module(global->module);
  ModuleGlobalMap::iterator global_it = global_module.symbols.find(global);
  if (global_it != global_module.symbols.end())
    return global_it->second;
  
  m_in_progress_globals.insert(global);
  
  typedef std::map<TreePtr<ModuleGlobal>, PSI_STD::set<TreePtr<ModuleGlobal> > > DependencyMapType;
  DependencyMapType dependency_map;
  std::vector<TreePtr<ModuleGlobal> > queue;
  queue.push_back(global);
  while (!queue.empty()) {
    TreePtr<ModuleGlobal> current = queue.back();
    queue.pop_back();

    PSI_STD::set<TreePtr<ModuleGlobal> >& dependencies = dependency_map[current];
    current->global_dependencies(dependencies);
    
    for (PSI_STD::set<TreePtr<ModuleGlobal> >::const_iterator ii = dependencies.begin(), ie = dependencies.end(); ii != ie; ++ii) {
      // If this global is "in progress", it cannot be built because we must
      // execute a function which expects it to exist in order to create it!
      if (m_in_progress_globals.find(*ii) != m_in_progress_globals.end()) {
        CompileError err(compile_context(), global->location());
        err.info("Circular dependency amongst global variables");
        for (std::set<TreePtr<ModuleGlobal> >::iterator ii = m_in_progress_globals.begin(), ie = m_in_progress_globals.end(); ii != ie; ++ii) {
          if (*ii != global)
            err.info(ii->location(), "Circular dependency");
        }
        err.end();
        throw CompileException();
      }
      
      // If this global has already been built, don't rebuild it
      TvmModule& module = get_module((*ii)->module);
      if (module.symbols.find(global) != module.symbols.end())
        continue;
      
      if (dependency_map.find(*ii) == dependency_map.end()) {
        dependency_map[*ii]; // Insert element into map to prevent duplication
        queue.push_back(*ii);
      }
    }
  }
  
  // Erase anything from the dependency map which has been built
  // during construction of the dependency map
  for (DependencyMapType::iterator ii = dependency_map.begin(), ie = dependency_map.end(), in; ii != ie; ii = in) {
    in = ii; ++in;
    
    TvmModule& module = get_module(ii->first->module);
    if (module.symbols.find(ii->first) != module.symbols.end())
      dependency_map.erase(ii);
  }
  
  // Remove any dependencies which have already been built
  /// \todo Need to check inter-module depenencies form a DAG here
  for (DependencyMapType::iterator ii = dependency_map.begin(), ie = dependency_map.end(), in; ii != ie; ++ii) {
    for (DependencyMapType::mapped_type::iterator ji = ii->second.begin(), je = ii->second.end(), jn; ji != je; ji = jn) {
      jn = ji; ++jn;
      if (dependency_map.find(*ji) == dependency_map.end())
        ii->second.erase(ji);
    }
  }
  
  // Break into initialisation sets to try and initialise dependent variables in the correct order
  // Note that dependencies should only occur one way between modules
  
  // Compute transitive closure of dependency map
  for (DependencyMapType::iterator ii = dependency_map.begin(), ie = dependency_map.end(); ii != ie; ++ii) {
    PSI_ASSERT(queue.empty());
    queue.assign(ii->second.begin(), ii->second.end());
    while (!queue.empty()) {
      DependencyMapType::iterator current = dependency_map.find(ii->first);
      PSI_ASSERT(current != dependency_map.end());
      queue.pop_back();
      
      for (DependencyMapType::mapped_type::iterator ji = current->second.begin(), je = current->second.end(); ji != je; ++ji) {
        if (ii->second.insert(*ji).second)
          queue.push_back(*ji);
      }
    }
  }
  
  // Group globals into interdependent sets
  typedef std::vector<std::pair<std::vector<TreePtr<ModuleGlobal> >, std::set<TreePtr<ModuleGlobal> > > > GlobalGroupsList;
  GlobalGroupsList global_groups;
  while (!dependency_map.empty()) {
    std::set<TreePtr<ModuleGlobal> > group;
    std::set<TreePtr<ModuleGlobal> > external_dependencies;
    
    DependencyMapType::iterator current = dependency_map.begin();
    // Make sure it doesn't depend on itself, which it will if there is a cycle
    current->second.erase(current->first);
    group.insert(current->first);
    for (DependencyMapType::mapped_type::const_iterator ji = current->second.begin(), je = current->second.end(), jn; ji != je; ++ji) {
      DependencyMapType::iterator dep = dependency_map.find(*ji);
      if ((dep != dependency_map.end()) && (dep->second.find(current->first) != dep->second.end())) {
        group.insert(*ji);
        dependency_map.erase(dep);
      } else {
        external_dependencies.insert(*ji);
      }
    }
    
    global_groups.push_back(std::make_pair(std::vector<TreePtr<ModuleGlobal> >(group.begin(), group.end()), external_dependencies));
    dependency_map.erase(current);
  }
  
  // Topological sort on groups
  std::vector<std::vector<TreePtr<ModuleGlobal> > > global_groups_sorted;
  while (!global_groups.empty()) {
    std::set<TreePtr<ModuleGlobal> > newly_sorted;
    // Find groups with no remaining dependencies and move them into the sorted list
    for (GlobalGroupsList::iterator ii = global_groups.begin(); ii != global_groups.end();) {
      if (ii->second.empty()) {
        global_groups_sorted.push_back(ii->first);
        newly_sorted.insert(ii->first.begin(), ii->first.end());
        ii = global_groups.erase(ii);
      } else {
        ++ii;
      }
    }
    
    PSI_ASSERT(!newly_sorted.empty());
    // Remove dependencies on globals newly added to the sorted list
    for (GlobalGroupsList::iterator ii = global_groups.begin(); ii != global_groups.end(); ++ii) {
      for (std::set<TreePtr<ModuleGlobal> >::iterator ji = newly_sorted.begin(), je = newly_sorted.end(); ji != je; ++ji)
        ii->second.erase(*ji);
    }
  }
  
  for (std::vector<std::vector<TreePtr<ModuleGlobal> > >::iterator ii = global_groups_sorted.begin(), ie = global_groups_sorted.end(); ii != ie; ++ii)
    build_global_group(*ii);
  
  m_in_progress_globals.erase(global);
  
  global_it = global_module.symbols.find(global);
  PSI_ASSERT(global_it != global_module.symbols.end());
  return global_it->second;
}

/**
 * \brief Build a group of globals.
 * 
 * Dependencies ordering has already been handled by build_module_global, which is the
 * only function which should call this one.
 */
void TvmCompiler::build_global_group(const std::vector<TreePtr<ModuleGlobal> >& group) {
  TvmModule& tvm_module = get_module(group.front()->module);
  tvm_module.jit_current = false;

  // Create storage for all of these globals
  bool pure_functional = true;
  for (std::vector<TreePtr<ModuleGlobal> >::const_iterator ii = group.begin(), ie = group.end(); ii != ie; ++ii) {
    const TreePtr<ModuleGlobal>& global = *ii;
    if (global->module != group.front()->module) {
      CompileError err(compile_context(), global->location());
      err.info(global->location(), "Circular dependency amongst globals in different modules");
      for (std::vector<TreePtr<ModuleGlobal> >::const_iterator ji = group.begin(), je = group.end(); ji != je; ++ji)
        err.info(ji->location(), "Dependency loop element");
      err.end();
      throw CompileException();
    }
      
    std::string symbol_name = mangle_name(global->location().logical);
    
    TvmResult type = m_functional_builder.build_type(global->type);

    if (TreePtr<Function> function = dyn_treeptr_cast<Function>(global)) {
      Tvm::ValuePtr<Tvm::FunctionType> tvm_ftype = Tvm::dyn_cast<Tvm::FunctionType>(type.value());
      if (!tvm_ftype)
        compile_context().error_throw(function->location(), "Type of function is not a function type");
      Tvm::ValuePtr<Tvm::Function> tvm_func = tvm_module.module->new_function(symbol_name, tvm_ftype, function->location());
      tvm_module.symbols.insert(std::make_pair(function, tvm_func));
    } else if (TreePtr<GlobalVariable> global_var = dyn_treeptr_cast<GlobalVariable>(global)) {
      Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = tvm_module.module->new_global_variable(symbol_name, type.value(), global_var->location());
      tvm_module.symbols.insert(std::make_pair(global_var, tvm_gvar));
      pure_functional = pure_functional && global_var->value->pure_functional();
    } else {
      PSI_FAIL("Unknown module global type");
    }
  }
  
  // First, generate functions
  for (std::vector<TreePtr<ModuleGlobal> >::const_iterator ii = group.begin(), ie = group.end(); ii != ie; ++ii) {
    if (TreePtr<Function> function = dyn_treeptr_cast<Function>(*ii)) {
      Tvm::ValuePtr<Tvm::Function> tvm_func = Tvm::value_cast<Tvm::Function>(tvm_module.symbols.find(*ii)->second);
      tvm_lower_function(*this, function, tvm_func);
      tvm_func->set_private(function->local);
    }
  }
  
  if (pure_functional) {
    // Can generate globals entirely as constant data
    for (std::vector<TreePtr<ModuleGlobal> >::const_iterator ii = group.begin(), ie = group.end(); ii != ie; ++ii) {
      if (TreePtr<GlobalVariable> gvar = dyn_treeptr_cast<GlobalVariable>(*ii)) {
        Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = Tvm::value_cast<Tvm::GlobalVariable>(tvm_module.symbols.find(*ii)->second);
        Tvm::ValuePtr<> value = m_functional_builder.build_value(gvar->value);
        tvm_gvar->set_value(value);
        tvm_gvar->set_constant(gvar->constant);
        tvm_gvar->set_merge(gvar->merge);
        tvm_gvar->set_private(gvar->local);
      }
    }
  } else {
    // Must generate an initialisation function
    PSI_NOT_IMPLEMENTED();
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

namespace {
  class GenericTypeCallback : public TvmFunctionalBuilderCallback {
  public:
    typedef boost::unordered_map<TreePtr<Anonymous>, Tvm::ValuePtr<> > AnonymousMapType;
    
  private:
    TvmCompiler *m_self;
    const AnonymousMapType *m_parameters;
    
  public:
    GenericTypeCallback(TvmCompiler *self, const AnonymousMapType *parameters)
    : m_self(self), m_parameters(parameters) {}
    
    virtual TvmResult build_hook(const TreePtr<Term>& term) {
      if (TreePtr<Anonymous> anon = dyn_treeptr_cast<Anonymous>(term)) {
        AnonymousMapType::const_iterator ii = m_parameters->find(anon);
        if (ii == m_parameters->end())
          m_self->compile_context().error_throw(term->location(), "Unrecognised anonymous parameter");
        return TvmResult::type(anon->type, ii->second, false);
      } else {
        m_self->compile_context().error_throw(term->location(), boost::format("Unsupported term type in generic parameter: %s") % si_vptr(term.get())->classname);
      }
    }
    
    virtual TvmResult build_define_hook(const TreePtr<GlobalDefine>& define) {
      return m_self->build(define);
    }
    
    virtual TvmGenericResult build_generic_hook(const TreePtr<GenericType>& generic) {
      return m_self->build_generic(generic);
    }
    
    virtual Tvm::ValuePtr<> load_hook(const Tvm::ValuePtr<>& PSI_UNUSED(ptr), const SourceLocation& PSI_UNUSED(location)) {
      PSI_FAIL("Cannot create global load instruction");
    }
  };
}

/**
 * \brief Lower a generic type.
 */
TvmGenericResult TvmCompiler::build_generic(const TreePtr<GenericType>& generic) {
  GenericTypeMap::iterator gen_it = m_generics.find(generic);
  if (gen_it != m_generics.end())
    return gen_it->second;
  
  PSI_STD::vector<TreePtr<Term> > anonymous_list;
  Tvm::RecursiveType::ParameterList parameters;
  GenericTypeCallback::AnonymousMapType parameter_map;
  GenericTypeCallback type_callback(this, &parameter_map);
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = generic->pattern.begin(), ie = generic->pattern.end(); ii != ie; ++ii) {
    // Need to rewrite parameter to anonymous to build lowered type with RecursiveParameter
    // Would've made more seense if I'd built the two systems with a more similar parameter convention.
    TreePtr<Term> rewrite_type = (*ii)->type->specialize(generic->location(), anonymous_list);
    TreePtr<Anonymous> rewrite_anon(new Anonymous(rewrite_type, rewrite_type->location()));
    anonymous_list.push_back(rewrite_anon);
    Tvm::ValuePtr<> type = type_callback.build_hook((*ii)->type).value();
    Tvm::ValuePtr<Tvm::RecursiveParameter> param = Tvm::RecursiveParameter::create(type, false, ii->location());
    parameter_map.insert(std::make_pair(rewrite_anon, param));
    parameters.push_back(*param);
  }
  
  Tvm::ValuePtr<Tvm::RecursiveType> recursive =
    Tvm::RecursiveType::create(Tvm::FunctionalBuilder::type_type(m_tvm_context, generic->location()), parameters, generic.location());
  TvmGenericResult result;
  result.generic = recursive;
  result.primitive = m_functional_builder.is_primitive(generic->member_type);
  
  // Insert generic into map before building it because it may recursively reference itself.
  m_generics.insert(std::make_pair(generic, result));
  
  GenericTypeCallback builder_callback(this, &parameter_map);
  TvmFunctionalBuilder builder(m_compile_context, &m_tvm_context, &builder_callback);
  recursive->resolve(builder.build_value(generic->member_type));
  
  return result;
}
}
}
