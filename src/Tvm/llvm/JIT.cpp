#include "Builder.hpp"
#include "../Jit.hpp"

#include <boost/format.hpp>
#include <boost/scoped_ptr.hpp>

#include "LLVMPushWarnings.hpp"
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#if PSI_DEBUG
#include <llvm/Analysis/Verifier.h>
#endif
#include "LLVMPopWarnings.hpp"

#if PSI_DEBUG
#include <iostream>
#endif

namespace Psi {
namespace Tvm {
namespace LLVM {

typedef boost::unordered_map<ValuePtr<Global>, void*> ModuleJitMapping;

struct LLVMJitModule {
  boost::shared_ptr<llvm::ExecutionEngine> jit;
  ModuleMapping mapping;
  ModuleJitMapping jit_mapping;
  std::size_t load_priority;
};

class LLVMJit : public Jit {
public:
  LLVMJit(const CompileErrorPair& error_loc, const std::string&, const boost::shared_ptr<llvm::TargetMachine>&, const PropertyValue& config);
  virtual ~LLVMJit();
  virtual void destroy();

  CompileErrorContext& error_context() {return *m_error_context;}
  virtual void add_module(Module*);
  virtual void remove_module(Module*);
  virtual void* get_symbol(const ValuePtr<Global>&);

private:
  PropertyValue m_config;
  CompileErrorContext *m_error_context;
  llvm::LLVMContext m_llvm_context;
  llvm::PassManager m_llvm_module_pass;
  llvm::CodeGenOpt::Level m_llvm_opt;
  TargetCallback m_target_callback;
  boost::shared_ptr<llvm::TargetMachine> m_target_machine;
  std::size_t m_load_priority_max;
  boost::unordered_map<Module*, LLVMJitModule> m_modules;
  typedef boost::unordered_map<std::string, void*> ExportedSymbolMap;
  ExportedSymbolMap m_exported_symbols;

  void populate_pass_manager(llvm::PassManager& pm);
  
  static bool symbol_lookup(void **result, const char *name, void *user_ptr);
};

LLVMJit::LLVMJit(const CompileErrorPair& error_loc,
                 const std::string& host_triple,
                 const boost::shared_ptr<llvm::TargetMachine>& host_machine,
                 const PropertyValue& config)
: m_config(config),
m_error_context(&error_loc.context()),
m_target_callback(error_loc, &m_llvm_context, host_machine, host_triple),
m_target_machine(host_machine),
m_load_priority_max(0) {
  populate_pass_manager(m_llvm_module_pass);
}

LLVMJit::~LLVMJit() {
  m_modules.clear();
}

void LLVMJit::destroy() {
  // Run module destructor functions
  std::vector<std::pair<std::size_t, llvm::ExecutionEngine*> > load_order;
  for (boost::unordered_map<Module*, LLVMJitModule>::const_iterator ii = m_modules.begin(), ie = m_modules.end(); ii != ie; ++ii)
    load_order.push_back(std::make_pair(ii->second.load_priority, ii->second.jit.get()));
    
  std::sort(load_order.begin(), load_order.end());
  for (std::vector<std::pair<std::size_t, llvm::ExecutionEngine*> >::reverse_iterator ii = load_order.rbegin(), ie = load_order.rend(); ii != ie; ++ii)
    ii->second->runStaticConstructorsDestructors(true);
  
  delete this;
}

void LLVMJit::populate_pass_manager(llvm::PassManager& pm) {
#if PSI_DEBUG
  pm.add(llvm::createVerifierPass(llvm::AbortProcessAction));
#endif
  pm.add(new llvm::TargetLibraryInfo(llvm::Triple(m_target_machine->getTargetTriple())));
  m_target_machine->addAnalysisPasses(pm);
  if (const llvm::DataLayout *td = m_target_machine->getDataLayout())
    pm.add(new llvm::DataLayout(*td));

  llvm::PassManagerBuilder pb;
  pb.OptLevel = 0;
  
  if (boost::optional<int> opt_level = m_config.path_int("opt"))
    pb.OptLevel = *opt_level;
  
  if (pb.OptLevel >= 2)
    m_llvm_opt = llvm::CodeGenOpt::Aggressive;         
  else
    m_llvm_opt = llvm::CodeGenOpt::Default;

  pb.populateModulePassManager(m_llvm_module_pass);
}

namespace {
  /// Can symbols with the given linkage mode be shared between object files in the same shared o
  bool is_linkage_shared(Linkage l) {
    return (l != link_import) && (l != link_local);
  }

  typedef boost::unordered_map<std::string, void*> SymbolAddressMap;
  
  void object_notify_emitted(const llvm::ObjectImage& obj, void *user_ptr) {
    SymbolAddressMap& sym_map = *static_cast<SymbolAddressMap*>(user_ptr);

    llvm::error_code err;
    llvm::StringRef name;
    uint64_t addr;
    llvm::object::SymbolRef::Type type;
    for (llvm::object::symbol_iterator ii = obj.begin_symbols(), ie = obj.end_symbols(); ii != ie; ii.increment(err)) {
      ii->getType(type);
      if ((type == llvm::object::SymbolRef::ST_Data) || (type == llvm::object::SymbolRef::ST_Function)) {
        ii->getName(name);
        ii->getAddress(addr);
        sym_map[name] = reinterpret_cast<void*>(addr);
      }
    }
  }
}

void LLVMJit::add_module(Module *module) {
  if (m_modules.find(module) != m_modules.end())
    error_context().error_throw(module->location(), "module already exists in this JIT");

  LLVMJitModule mapping;

  std::auto_ptr<llvm::Module> llvm_module_auto(new llvm::Module(module->name(), m_llvm_context));
  llvm::Module *llvm_module = llvm_module_auto.get();
  
  llvm_module->setTargetTriple(m_target_machine->getTargetTriple());
  llvm_module->setDataLayout(m_target_machine->getDataLayout()->getStringRepresentation());
  
  ModuleBuilder builder(&error_context(), &m_llvm_context, m_target_machine.get(), llvm_module, &m_target_callback);
  mapping.mapping = builder.run(module);
  
#if PSI_DEBUG
  if (const char *debug_mode = std::getenv("PSI_LLVM_DEBUG")) {
    if ((std::strcmp(debug_mode, "all") == 0) || (std::strcmp(debug_mode, "ir") == 0))
      llvm_module->dump();
  }
#endif

  m_llvm_module_pass.run(*llvm_module);
  
  mapping.jit.reset(psi_tvm_llvm_make_execution_engine(llvm_module_auto.release(), m_llvm_opt, m_target_machine->Options,
                                                        &LLVMJit::symbol_lookup, this));
  PSI_ASSERT_MSG(mapping.jit, "LLVM JIT creation failed - most likely the JIT has not been linked in");

  SymbolAddressMap symbol_map;
  boost::scoped_ptr<llvm::JITEventListener> listener(psi_tvm_llvm_make_object_notify_wrapper(&object_notify_emitted, &symbol_map));
  mapping.jit->RegisterJITEventListener(listener.get());
  mapping.load_priority = 0;
  mapping.jit->finalizeObject();
  mapping.jit->UnregisterJITEventListener(listener.get());
  listener.reset();
  
  std::pair<boost::unordered_map<Module*, LLVMJitModule>::iterator, bool> ins_result = m_modules.insert(std::make_pair(module, mapping));
  PSI_ASSERT(ins_result.second);
  
  LLVMJitModule& jit_module = ins_result.first->second;

  // Add to global symbol list
  for (ModuleMapping::const_iterator ii = jit_module.mapping.begin(), ie = jit_module.mapping.end(); ii != ie; ++ii) {
    if (is_linkage_shared(ii->first->linkage())) {
      SymbolAddressMap::iterator ji = symbol_map.find(ii->second->getName());
      PSI_ASSERT(ji != symbol_map.end());
      jit_module.jit_mapping.insert(std::make_pair(ii->first, ji->second));
      m_exported_symbols[ii->first->name()] = ji->second;
    }
  }

  jit_module.load_priority = ++m_load_priority_max;
  jit_module.jit->runStaticConstructorsDestructors(false);
}

void LLVMJit::remove_module(Module *module) {
  boost::unordered_map<Module*, LLVMJitModule>::iterator it = m_modules.find(module);
  if (it == m_modules.end())
    error_context().error_throw(module->location(), "module not present");
  
  LLVMJitModule& jit_module = it->second;
  for (ModuleJitMapping::const_iterator ii = jit_module.jit_mapping.begin(), ie = jit_module.jit_mapping.end(); ii != ie; ++ii) {
    ExportedSymbolMap::iterator ji = m_exported_symbols.find(ii->first->name());
    if (ji != m_exported_symbols.end()) {
      if (ji->second == ii->second)
        m_exported_symbols.erase(ji);
    }
  }

  jit_module.jit->runStaticConstructorsDestructors(true);
  m_modules.erase(it);
}

void* LLVMJit::get_symbol(const ValuePtr<Global>& global) {
  Module *module = global->module();
  boost::unordered_map<Module*, LLVMJitModule>::iterator it = m_modules.find(module);
  if (it == m_modules.end())
    error_context().error_throw(global->location(), "Module does not appear to be available in this JIT");
  
  ModuleJitMapping::const_iterator jt = it->second.jit_mapping.find(global);
  PSI_ASSERT(jt != it->second.jit_mapping.end());
  return jt->second;
}

/**
  * Symbol resolver callback.
  */
bool LLVMJit::symbol_lookup(void** result, const char* name, void* user_ptr) {
  LLVMJit& self = *static_cast<LLVMJit*>(user_ptr);
  
  std::string name_s(name);
  ExportedSymbolMap::const_iterator it = self.m_exported_symbols.find(name_s);
  if (it != self.m_exported_symbols.end()) {
    *result = it->second;
    return true;
  }
  
  // Use LLVMs normal symbol resolution
  return false;
}
}
}
}

PSI_TVM_JIT_EXPORT(llvm, error_handler, config) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
  
  llvm::Triple triple = Psi::Tvm::LLVM::TargetCallback::jit_triple();
  
  std::string error_msg;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(triple.str(), error_msg);
  if (!target)
    error_handler.error_throw("Could not get LLVM target: " + error_msg);

  llvm::TargetOptions target_opts;
  target_opts.JITEmitDebugInfo = 1;

  boost::shared_ptr<llvm::TargetMachine> tm(target->createTargetMachine(triple.str(), "", "", target_opts));
  if (!tm)
    error_handler.error_throw("Failed to create target machine");
  
  return new Psi::Tvm::LLVM::LLVMJit(error_handler, triple.str(), tm, config);
}
