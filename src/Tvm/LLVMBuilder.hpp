#ifndef HPP_PSI_TVM_LLVMBUILDER
#define HPP_PSI_TVM_LLVMBUILDER

#include <deque>
#include <tr1/cstdint>
#include <tr1/unordered_map>

#include "Core.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * \brief Value type of MetatypeTerm.
     *
     * This is here for easy interfacing with C++ and must be kept in
     * sync with LLVMConstantBuilder::metatype_type.
     */
    struct MetatypeValue {
      std::tr1::uint64_t size;
      std::tr1::uint64_t align;
    };

    class LLVMValueBuilder {
    public:
      ~LLVMValueBuilder();

      llvm::LLVMContext& context() {return *m_context;}
      llvm::Module& module() {return *m_module;}
      llvm::GlobalValue* global(TermRef<GlobalTerm> term);
      LLVMValue value(TermRef<Term> term);
      LLVMType type(TermRef<Term> term);

      void set_module(llvm::Module *module);

      LLVMType metatype_type();
      LLVMValue metatype_value(std::size_t size, std::size_t align);
      LLVMValue metatype_value_empty();
      LLVMValue metatype_value_from_type(const llvm::Type* ty);
      LLVMValue metatype_value_from_constant(llvm::Constant *size, llvm::Constant *align);

    protected:
      LLVMValueBuilder(llvm::LLVMContext *context, llvm::Module *module);
      LLVMValueBuilder(LLVMValueBuilder *parent);

    private:
      LLVMValueBuilder *m_parent;
      llvm::LLVMContext *m_context;
      llvm::Module *m_module;

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
    };

    class LLVMFunctionBuilder : public LLVMValueBuilder {
      friend class LLVMValueBuilder;

    public:
      typedef llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

      ~LLVMFunctionBuilder();

      llvm::Function* function() {return m_function;}
      IRBuilder& irbuilder() {return *m_irbuilder;}
      CallingConvention calling_convention() const {return m_calling_convention;}

      LLVMValue metatype_value_runtime(llvm::Value *size, llvm::Value *align);

    private:
      LLVMFunctionBuilder(LLVMValueBuilder *constant_builder, llvm::Function *function, IRBuilder *irbuilder, CallingConvention calling_convention);

      llvm::Function *m_function;
      IRBuilder *m_irbuilder;
      CallingConvention m_calling_convention;
    };

    llvm::Function* llvm_intrinsic_memcpy(llvm::Module& m);
  }
}

#endif
