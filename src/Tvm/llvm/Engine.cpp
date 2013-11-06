#include "Engine.hpp"

#include <string>

#include "LLVMPushWarnings.hpp"
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>

/*
 * Do not remove the MCJIT.h include. Although everything will build
 * fine, the JIT will not be available since MCJIT.h includes some magic
 * which ensures the JIT is really available.
 */
#include <llvm/ExecutionEngine/MCJIT.h>
#include "LLVMPopWarnings.hpp"

namespace {
typedef bool (*SymbolCallbackType) (void**, const char*, void*);
typedef void (*ObjectNotifyCallback) (const llvm::ObjectImage&, void*);

/// See http://blog.llvm.org/2013/07/using-mcjit-with-kaleidoscope-tutorial.html
class CallbackMemoryManagerMC : public llvm::SectionMemoryManager {
  CallbackMemoryManagerMC(const CallbackMemoryManagerMC&) LLVM_DELETED_FUNCTION;
  void operator=(const CallbackMemoryManagerMC&) LLVM_DELETED_FUNCTION;

public:
  
  CallbackMemoryManagerMC(SymbolCallbackType symbol_callback, void *user_ptr)
  : m_symbol_callback(symbol_callback), m_user_ptr(user_ptr) {}
  virtual ~CallbackMemoryManagerMC() {}
  
  virtual uint64_t getSymbolAddress(const std::string& name) {
    void *result;
    if (m_symbol_callback(&result, name.c_str(), m_user_ptr))
      return (uint64_t)result;
    
    return llvm::SectionMemoryManager::getSymbolAddress(name);
  }

private:
  SymbolCallbackType m_symbol_callback;
  void *m_user_ptr;
};

class ObjectNotifyCallbackWrapper : public llvm::JITEventListener {
  void *m_user_ptr;
  ObjectNotifyCallback m_emitted;
  
public:
  ObjectNotifyCallbackWrapper(void *user_ptr, ObjectNotifyCallback emitted)
  : m_user_ptr(user_ptr), m_emitted(emitted) {}
  
  virtual void NotifyObjectEmitted(const llvm::ObjectImage& obj) {
    m_emitted(obj, m_user_ptr);
  }
};
}

extern "C" llvm::JITEventListener* psi_tvm_llvm_make_object_notify_wrapper(ObjectNotifyCallback emitted, void *user_ptr) {
  return new ObjectNotifyCallbackWrapper(user_ptr, emitted);
}

extern "C" llvm::ExecutionEngine* psi_tvm_llvm_make_execution_engine(llvm::Module *module, llvm::CodeGenOpt::Level opt_level, const llvm::TargetOptions& target_opts,
                                                                     SymbolCallbackType symbol_callback, void *user_ptr) {
  llvm::SectionMemoryManager *memory_manager = NULL;
  llvm::ExecutionEngine *ee = NULL;
  
  llvm::EngineBuilder eb(module);
  eb.setEngineKind(llvm::EngineKind::JIT);
  eb.setOptLevel(opt_level);
  eb.setTargetOptions(target_opts);
  eb.setUseMCJIT(true);
  if (!(memory_manager = new (std::nothrow) CallbackMemoryManagerMC(symbol_callback, user_ptr)))
    goto error_1;
  eb.setMCJITMemoryManager(memory_manager);
  
  ee = eb.create();
  if (ee)
    return ee;

  delete memory_manager;
error_1:
  delete module;
  
  return NULL;
}
