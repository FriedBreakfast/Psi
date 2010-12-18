#ifndef HPP_PSI_TVM_LLVMBUILDER
#define HPP_PSI_TVM_LLVMBUILDER

#include <deque>
#include <tr1/cstdint>
#include <tr1/unordered_map>
#include <stdexcept>

#include <boost/optional.hpp>

#include "Core.hpp"
#include "BigInteger.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    typedef llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > LLVMIRBuilder;

    class LLVMFunctionBuilder;

    /**
     * Thrown when an error occurs during LLVM construction: many of
     * these use PSI_ASSERT, but this can also be used when the error
     * condition has not been tested well enough.
     */
    class LLVMBuildError : public std::logic_error {
    public:
      explicit LLVMBuildError(const std::string& message);
    };

    class LLVMConstantBuilder {
      friend class LLVMGlobalBuilder;
      friend class LLVMFunctionBuilder;

    public:
      ~LLVMConstantBuilder();

      llvm::LLVMContext& llvm_context() {return *m_llvm_context;}

      llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}
      const llvm::Type* build_type(Term *term);
      llvm::Constant* build_constant(Term *term);
      BigInteger build_constant_integer(Term *term);

      const llvm::IntegerType *size_type();
      std::size_t type_size(const llvm::Type *ty);
      std::size_t type_alignment(const llvm::Type *ty);

      static llvm::APInt bigint_to_apint(const BigInteger&, unsigned, bool=true, bool=false);
      static BigInteger apint_to_bigint(const llvm::APInt&);

    private:
      LLVMConstantBuilder(LLVMConstantBuilder *parent);
      LLVMConstantBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine);

      LLVMConstantBuilder *m_parent;
      llvm::LLVMContext *m_llvm_context;
      llvm::TargetMachine *m_llvm_target_machine;

      typedef std::tr1::unordered_map<Term*, boost::optional<const llvm::Type*> > TypeTermMap;
      TypeTermMap m_type_terms;

      typedef std::tr1::unordered_map<Term*, llvm::Constant*> ConstantTermMap;
      ConstantTermMap m_constant_terms;
    };

    class LLVMGlobalBuilder : public LLVMConstantBuilder {
    public:
      LLVMGlobalBuilder(llvm::LLVMContext *context, llvm::Module *module, llvm::TargetMachine *target_machine=0);
      ~LLVMGlobalBuilder();

      static llvm::TargetMachine* llvm_host_machine();

      void set_module(llvm::Module *module);

      llvm::Module& llvm_module() {return *m_module;}

      llvm::GlobalValue* build_global(GlobalTerm *term);

    protected:
      llvm::Module *m_module;

      /// Global terms which have been encountered but not yet built
      typedef std::deque<std::pair<GlobalTerm*, llvm::GlobalValue*> > GlobalTermBuildList;
      GlobalTermBuildList m_global_build_list;

      typedef std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> GlobalTermMap;
      GlobalTermMap m_global_terms;
    };

    class LLVMFunctionBuilder : public LLVMConstantBuilder {
      friend class LLVMGlobalBuilder;

    public:
      typedef std::tr1::unordered_map<Term*, LLVMValue> ValueTermMap;

      ~LLVMFunctionBuilder();

      llvm::Module& llvm_module() {return m_constant_builder->llvm_module();}

      FunctionTerm *function() {return m_function;}
      llvm::Function* llvm_function() {return m_llvm_function;}
      LLVMIRBuilder& irbuilder() {return *m_irbuilder;}

      /// Returns the maximum alignment for any type supported. This
      /// seems to have to be hardwired which is bad, but 8 should be
      /// enough for all current platforms.
      unsigned unknown_alloca_align() const {return 8;}

      LLVMValue build_value(Term *term);
      llvm::Value* build_known_value(Term *term);

      llvm::Value* cast_pointer_to_generic(llvm::Value *value);
      llvm::Value* cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type);

      llvm::Instruction* create_alloca(llvm::Value *size);
      llvm::Value* create_alloca_for(Term *type);
      void create_memcpy(llvm::Value *dest, llvm::Value *src, llvm::Value *count);
      void create_store(llvm::Value *dest, Term *src);
      void create_store_unknown(llvm::Value *dest, llvm::Value *src, Term *type);

      llvm::StringRef term_name(Term *term);

    private:
      LLVMFunctionBuilder(LLVMGlobalBuilder *constant_builder, FunctionTerm *function,
                          llvm::Function *llvm_function, LLVMIRBuilder *irbuilder);

      LLVMGlobalBuilder *m_constant_builder;
      LLVMIRBuilder *m_irbuilder;

      FunctionTerm *m_function;
      llvm::Function *m_llvm_function;

      ValueTermMap m_value_terms;

      void run();
      llvm::BasicBlock* build_function_entry();
      void build_phi_alloca(std::tr1::unordered_map<PhiTerm*, llvm::Value*>& phi_storage_map,
                            const std::vector<BlockTerm*>& dominated);
      void simplify_stack_save_restore();
      llvm::CallInst* first_stack_restore(llvm::BasicBlock *block);
      bool has_outstanding_alloca(llvm::BasicBlock *block);
    };

    namespace LLVMIntrinsics {
      llvm::Function* memcpy(llvm::Module& m);
      llvm::Function* stacksave(llvm::Module& m);
      llvm::Function* stackrestore(llvm::Module& m);
    }

    /**
     * Functions for generating the LLVM type of and LLVM values for
     * #Metatype.
     */
    namespace LLVMMetatype {
      const llvm::Type* type(LLVMConstantBuilder&);
      llvm::Constant* from_constant(LLVMConstantBuilder&, const BigInteger& size, const BigInteger& align);
      llvm::Constant* from_type(LLVMConstantBuilder& builder, const llvm::Type* ty);
      LLVMValue from_value(LLVMFunctionBuilder&, llvm::Value *size, llvm::Value *align);

      BigInteger to_size_constant(llvm::Constant *value);
      BigInteger to_align_constant(llvm::Constant *value);
      llvm::Value* to_size_value(LLVMFunctionBuilder&, llvm::Value*);
      llvm::Value* to_align_value(LLVMFunctionBuilder&, llvm::Value*);
    }
  }
}

#endif
