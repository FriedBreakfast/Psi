#ifndef HPP_PSI_TVM_LLVM_ENGINE
#define HPP_PSI_TVM_LLVM_ENGINE

#include "LLVMPushWarnings.hpp"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/ObjectImage.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/IR/Module.h>
#include "LLVMPopWarnings.hpp"

extern "C" llvm::JITEventListener* psi_tvm_llvm_make_object_notify_wrapper(void (*emitted) (const llvm::ObjectImage&, void*), void *user_ptr);
extern "C" llvm::ExecutionEngine* psi_tvm_llvm_make_execution_engine(llvm::Module *module, llvm::CodeGenOpt::Level opt_level, const llvm::TargetOptions& target_opts,
                                                                     bool (*symbol_callback) (void**,const char*,void*), void *user_ptr);

#endif
