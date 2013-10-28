#ifndef HPP_PSI_TVM_LLVM_ENGINE
#define HPP_PSI_TVM_LLVM_ENGINE

#include "LLVMPushWarnings.hpp"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/Module.h>
#include "LLVMPopWarnings.hpp"

extern "C" llvm::ExecutionEngine* psi_tvm_llvm_make_execution_engine(llvm::Module *module, bool use_mcjit, llvm::CodeGenOpt::Level opt_level, const llvm::TargetOptions& target_opts,
                                                                     bool (*symbol_callback) (void**,const char*,void*), void *user_ptr);

#endif
