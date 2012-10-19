#include "TvmLowering.hpp"

#include "Tvm/FunctionalBuilder.hpp"
#include "Tvm/Function.hpp"

namespace Psi {
namespace Compiler {
TvmCompiler::TvmCompiler(CompileContext *compile_context)
: m_compile_context(compile_context) {
  boost::shared_ptr<Tvm::JitFactory> factory = Tvm::JitFactory::get("llvm");
  m_jit = factory->create_jit();
}

TvmCompiler::~TvmCompiler() {
}

/**
 * \brief Load a library.
 */
boost::shared_ptr<Platform::PlatformLibrary> TvmCompiler::load_library(const TreePtr<Library>& lib) {
  LibraryMap::iterator lib_it = m_libraries.find(lib);
  if (lib_it != m_libraries.end())
    return lib_it->second;
  
  PropertyValue pv = lib->callback->evaluate(m_local_target, m_local_target);
  boost::shared_ptr<Platform::PlatformLibrary> sys_lib = Platform::load_library(pv);
  m_libraries.insert(std::make_pair(lib, sys_lib));
  
  return sys_lib;
}

/**
 * \brief Create a Tvm::Global from a Global.
 */
Tvm::ValuePtr<Tvm::Global> TvmCompiler::build(const TreePtr<Global>& global) {
  if (TreePtr<ModuleGlobal> mod_global = dyn_treeptr_cast<ModuleGlobal>(global)) {
    TreePtr<Module> module = mod_global->module;
    if (TreePtr<ExternalGlobal> ext_global = dyn_treeptr_cast<ExternalGlobal>(global)) {
      PSI_NOT_IMPLEMENTED();
    } else {
      TvmModule& tvm_module = m_modules[module];
      if (!tvm_module.module)
        tvm_module.module.reset(new Tvm::Module(&m_tvm_context, module->name, module->location()));
      
      ModuleGlobalMap::iterator it = tvm_module.symbols.find(mod_global);
      if (it != tvm_module.symbols.end())
        return it->second;
      
      std::string symbol_name = "";
      
      Tvm::ValuePtr<> type = tvm_global_type(mod_global->type);

      if (TreePtr<Function> function = dyn_treeptr_cast<Function>(global)) {
        Tvm::ValuePtr<Tvm::FunctionType> tvm_ftype = Tvm::dyn_cast<Tvm::FunctionType>(type);
        if (!tvm_ftype)
          compile_context().error_throw(function->location(), "Type of function is not a function type");
        Tvm::ValuePtr<Tvm::Function> tvm_func = tvm_module.module->new_function(symbol_name, tvm_ftype, function->location());
        tvm_module.symbols.insert(std::make_pair(function, tvm_func));
        PSI_NOT_IMPLEMENTED();
        return tvm_func;
      } else if (TreePtr<GlobalVariable> global_var = dyn_treeptr_cast<GlobalVariable>(global)) {
        Tvm::ValuePtr<Tvm::GlobalVariable> tvm_gvar = tvm_module.module->new_global_variable(symbol_name, type, global_var->location());
        tvm_module.symbols.insert(std::make_pair(global_var, tvm_gvar));
        PSI_NOT_IMPLEMENTED();
        return tvm_gvar;
      } else {
        PSI_FAIL("Unknown module global type");
      }
    }
  } else if (TreePtr<LibrarySymbol> lib_global = dyn_treeptr_cast<LibrarySymbol>(global)) {
  } else {
    PSI_FAIL("Unknown global type");
  }
}

/**
 * \brief Just-in-time compile a symbol.
 */
void* TvmCompiler::jit_compile(const TreePtr<Global>& global) {
  Tvm::ValuePtr<Tvm::Global> built = build(global);
  return m_jit->get_symbol(built);
}

/**
 * \brief Convert a global type to TVM.
 */
Tvm::ValuePtr<> TvmCompiler::tvm_global_type(const TreePtr<Term>& type) {
  if (TreePtr<ArrayType> array_ty = dyn_treeptr_cast<ArrayType>(type)) {
    Tvm::ValuePtr<> element = tvm_global_type(array_ty->element_type);
    Tvm::ValuePtr<> length = tvm_global_value(array_ty->length);
    return Tvm::FunctionalBuilder::array_type(element, length, type->location());
  } else if (TreePtr<EmptyType> empty_ty = dyn_treeptr_cast<EmptyType>(type)) {
    return Tvm::FunctionalBuilder::empty_type(m_tvm_context, type->location());
  } else if (TreePtr<FunctionType> function_ty = dyn_treeptr_cast<FunctionType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<PointerType> pointer_ty = dyn_treeptr_cast<PointerType>(type)) {
    Tvm::ValuePtr<> target = tvm_global_type(pointer_ty->target_type);
    return Tvm::FunctionalBuilder::pointer_type(target, type->location());
  } else if (TreePtr<PrimitiveType> primitive_ty = dyn_treeptr_cast<PrimitiveType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<StructType> struct_ty = dyn_treeptr_cast<StructType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else if (TreePtr<UnionType> union_ty = dyn_treeptr_cast<UnionType>(type)) {
    PSI_NOT_IMPLEMENTED();
  } else {
    PSI_NOT_IMPLEMENTED();
  }
}

/**
 * \brief Convert a constant global value to TVM.
 */
Tvm::ValuePtr<Tvm::Value> TvmCompiler::tvm_global_value(const TreePtr<Term>& value) {
  PSI_NOT_IMPLEMENTED();
}
}
}
