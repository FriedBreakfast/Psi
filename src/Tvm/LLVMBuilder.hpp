#ifndef HPP_PSI_TVM_LLVMBUILDER
#define HPP_PSI_TVM_LLVMBUILDER

#include <deque>
#include <tr1/cstdint>
#include <tr1/unordered_map>

#include "Core.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    class LLVMValueBuilder {
    public:
      ~LLVMValueBuilder();

      llvm::LLVMContext& context() {return *m_context;}
      llvm::Module& module() {return *m_module;}
      llvm::GlobalValue* global(GlobalTerm* term);
      LLVMValue value(Term* term);
      LLVMType type(Term* term);

      void set_module(llvm::Module *module);
      void set_debug(llvm::raw_ostream *stream);

    protected:
      LLVMValueBuilder(llvm::LLVMContext *context, llvm::Module *module);
      LLVMValueBuilder(LLVMValueBuilder *parent);

      virtual LLVMValue value_impl(Term* term) = 0;

      LLVMValueBuilder *m_parent;
      llvm::LLVMContext *m_context;
      llvm::Module *m_module;
      llvm::raw_ostream *m_debug_stream;

      typedef std::tr1::unordered_map<Term*, LLVMType> TypeTermMap;
      TypeTermMap m_type_terms;

      typedef std::tr1::unordered_map<Term*, LLVMValue> ValueTermMap;
      ValueTermMap m_value_terms;

      /// Global terms which have been encountered but not yet built
      typedef std::deque<std::pair<Term*, llvm::GlobalValue*> > GlobalTermBuildList;
      GlobalTermBuildList m_global_build_list;

      typedef std::tr1::unordered_map<Term*, llvm::GlobalValue*> GlobalTermMap;
      GlobalTermMap m_global_terms;

      void build_global_variable(GlobalVariableTerm *psi_var, llvm::GlobalVariable *llvm_var);
      void build_function(FunctionTerm *psi_func, llvm::Function *llvm_func);
      llvm::BasicBlock* build_function_entry(FunctionTerm *psi_func, llvm::Function *llvm_func, LLVMFunctionBuilder& func_builder);
    };

    class LLVMConstantBuilder : public LLVMValueBuilder {
    public:
      LLVMConstantBuilder(llvm::LLVMContext *context, llvm::Module *module);
      ~LLVMConstantBuilder();

    private:
      virtual LLVMValue value_impl(Term* term);
    };

    class LLVMFunctionBuilder : public LLVMValueBuilder {
      friend class LLVMValueBuilder;

    public:
      typedef llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

      ~LLVMFunctionBuilder();

      llvm::Function* function() {return m_function;}
      IRBuilder& irbuilder() {return *m_irbuilder;}
      CallingConvention calling_convention() const {return m_calling_convention;}

      llvm::Function* llvm_memcpy() const {return m_llvm_memcpy;}
      llvm::Function* llvm_stacksave() const {return m_llvm_stacksave;}
      llvm::Function* llvm_stackrestore() const {return m_llvm_stackrestore;}

    private:
      LLVMFunctionBuilder(LLVMValueBuilder *constant_builder, llvm::Function *function, IRBuilder *irbuilder, CallingConvention calling_convention);
      virtual LLVMValue value_impl(Term* term);

      llvm::Function *m_function;
      IRBuilder *m_irbuilder;
      CallingConvention m_calling_convention;

      llvm::Function *m_llvm_memcpy;
      llvm::Function *m_llvm_stacksave;
      llvm::Function *m_llvm_stackrestore;
    };

    llvm::Function* llvm_intrinsic_memcpy(llvm::Module& m);
    llvm::Function* llvm_intrinsic_stacksave(llvm::Module& m);
    llvm::Function* llvm_intrinsic_stackrestore(llvm::Module& m);
  }
}

#endif
