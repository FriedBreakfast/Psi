#include "Engine.hpp"

#include <string>

/*
 * Do not remove the JIT.h include. Although everything will build
 * fine, the JIT will not be available since JIT.h includes some magic
 * which ensures the JIT is really available.
 */
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/MCJIT.h>

namespace {
typedef bool (*SymbolCallbackType) (void**, const char*, void*);

/// See http://blog.llvm.org/2013/07/using-mcjit-with-kaleidoscope-tutorial.html
class CallbackMemoryManagerMC : public llvm::SectionMemoryManager {
  CallbackMemoryManagerMC(const CallbackMemoryManagerMC&) LLVM_DELETED_FUNCTION;
  void operator=(const CallbackMemoryManagerMC&) LLVM_DELETED_FUNCTION;

public:
  
  CallbackMemoryManagerMC(SymbolCallbackType symbol_callback, void *user_ptr)
  : m_symbol_callback(symbol_callback), m_user_ptr(user_ptr) {}
  virtual ~CallbackMemoryManagerMC() {}

  virtual void *getPointerToNamedFunction(const std::string& name, bool AbortOnFailure = true) {
    void *result;
    if (m_symbol_callback(&result, name.c_str(), m_user_ptr))
      return result;
    
    return llvm::SectionMemoryManager::getPointerToNamedFunction(name, AbortOnFailure);
  }

private:
  SymbolCallbackType m_symbol_callback;
  void *m_user_ptr;
};
}

extern "C" llvm::ExecutionEngine* psi_tvm_llvm_make_execution_engine(llvm::Module *module, bool use_mcjit, llvm::CodeGenOpt::Level opt_level, const llvm::TargetOptions& target_opts,
                                                                     SymbolCallbackType symbol_callback, void *user_ptr) {
  llvm::JITMemoryManager *memory_manager = NULL;
  llvm::ExecutionEngine *ee = NULL;
  
  llvm::EngineBuilder eb(module);
  eb.setEngineKind(llvm::EngineKind::JIT);
  eb.setOptLevel(opt_level);
  eb.setTargetOptions(target_opts);
  
  if (use_mcjit) {
    eb.setUseMCJIT(true);
    if (!(memory_manager = new (std::nothrow) CallbackMemoryManagerMC(symbol_callback, user_ptr)))
      goto error_1;
    eb.setJITMemoryManager(memory_manager);
  }
  
  ee = eb.create();
  if (ee)
    return ee;

  delete memory_manager;
error_1:
  delete module;
  
  return NULL;
}
