#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <limits>
#include <deque>
#include <exception>
#include <boost/unordered_map.hpp>

#include "LLVMPushWarnings.hpp"
#include <llvm/ADT/Triple.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/PassManager.h>
#include <llvm/Support/TargetFolder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#if PSI_DEBUG
#include <llvm/ExecutionEngine/JITEventListener.h>
#endif
#include "LLVMPopWarnings.hpp"

#include "../Core.hpp"
#include "../AggregateLowering.hpp"
#include "../Jit.hpp"
#include "../Function.hpp"
#include "../Functional.hpp"
#include "../Instructions.hpp"
#include "../Number.hpp"
#include "../../Utility.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      typedef llvm::IRBuilder<true, llvm::TargetFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;
      
      struct ModuleMapping {
        llvm::Module *module;
        boost::unordered_map<ValuePtr<Global>, llvm::GlobalValue*> globals;
        ModuleMapping() : module(NULL) {}
      };
      
      class TargetCallback {
        llvm::Triple m_triple;
        UniquePtr<AggregateLoweringPass::TargetCallback> m_aggregate_lowering_callback;
        
      public:
        TargetCallback(const CompileErrorPair& error_loc, llvm::LLVMContext *context, const boost::shared_ptr<llvm::TargetMachine>& target_machine, const std::string& triple);
        
        /**
         * Get a callback class for use by the aggregate lowering pass.
         */
        AggregateLoweringPass::TargetCallback* aggregate_lowering_callback() {
          return m_aggregate_lowering_callback.get();
        }
        
        llvm::Function* exception_personality_routine(llvm::Module *module, const std::string& basename);
      };

      class ModuleBuilder {
      public:
        ModuleBuilder(CompileErrorContext *error_context, llvm::LLVMContext*, llvm::TargetMachine*, llvm::Module*, llvm::FunctionPassManager*, TargetCallback*);
        ~ModuleBuilder();
        
        /// \brief Get the context to use for error reporting
        CompileErrorContext& error_context() {return *m_error_context;}

        /// \brief Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return *m_llvm_context;}
        /// \brief Get the LLVM target triple
        const llvm::Triple& llvm_triple() {return m_llvm_triple;}
        /// \brief Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}
        
        TargetCallback *target_callback() {return m_target_callback;}

        llvm::Type* build_type(const ValuePtr<>& term);
        llvm::Constant* build_constant(const ValuePtr<>& term);
        llvm::GlobalValue* build_global(const ValuePtr<Global>& term);
        void build_constructor_list(const char *name, const Module::ConstructorList& constructors);

        const llvm::APInt& build_constant_integer(const ValuePtr<>& term);
        
        ModuleMapping run(Module*);
        
        llvm::Module* llvm_module() {return m_llvm_module;}
        
        llvm::Function* llvm_memcpy() {return m_llvm_memcpy;}
        llvm::Function* llvm_memset() {return m_llvm_memset;}
        llvm::Function* llvm_stacksave() {return m_llvm_stacksave;}
        llvm::Function* llvm_stackrestore() {return m_llvm_stackrestore;}
        llvm::Function* llvm_invariant_start() {return m_llvm_invariant_start;}
        llvm::Function* llvm_invariant_end() {return m_llvm_invariant_end;}
        llvm::Function* llvm_eh_exception() {return m_llvm_eh_exception;}
        llvm::Function* llvm_eh_selector() {return m_llvm_eh_selector;}
        llvm::Function* llvm_eh_typeid_for() {return m_llvm_eh_typeid_for;}

      private:
        CompileErrorContext *m_error_context;
        llvm::LLVMContext *m_llvm_context;
        llvm::Triple m_llvm_triple;
        llvm::TargetMachine *m_llvm_target_machine;
        llvm::FunctionPassManager *m_llvm_function_pass;
        llvm::Module *m_llvm_module;
        TargetCallback *m_target_callback;

        typedef boost::unordered_map<ValuePtr<>, llvm::Type*> TypeTermMap;
        TypeTermMap m_type_terms;

        llvm::Type* build_type_internal(const ValuePtr<FunctionalValue>& term);

        typedef boost::unordered_map<ValuePtr<Global>, llvm::GlobalValue*> GlobalTermMap;
        GlobalTermMap m_global_terms;

        typedef boost::unordered_map<ValuePtr<>, llvm::Constant*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

        llvm::Constant* build_constant_internal(const ValuePtr<FunctionalValue>& term);
        
        llvm::Function *m_llvm_memcpy, *m_llvm_memset, *m_llvm_stacksave, *m_llvm_stackrestore,
        *m_llvm_invariant_start, *m_llvm_invariant_end,
        *m_llvm_eh_exception, *m_llvm_eh_selector, *m_llvm_eh_typeid_for;

        llvm::GlobalValue::LinkageTypes llvm_linkage_for(Linkage linkage);
        void apply_linkage(Linkage linkage, llvm::GlobalValue *value);
      };

      class FunctionBuilder {
        friend class ModuleBuilder;

      public:
        ~FunctionBuilder();
        
        /// \brief Get the context to use for error reporting
        CompileErrorContext& error_context() {return module_builder()->error_context();}
        ModuleBuilder *module_builder() {return m_module_builder;}

        const ValuePtr<Function>& function() {return m_function;}
        llvm::Function* llvm_function() {return m_llvm_function;}
        llvm::LLVMContext& llvm_context() {return m_module_builder->llvm_context();}
        IRBuilder& irbuilder() {return m_irbuilder;}

        unsigned unknown_alloca_align();

        llvm::Value* build_value(const ValuePtr<>& term);

        llvm::StringRef term_name(const ValuePtr<>& term);
        
      private:
        FunctionBuilder(ModuleBuilder*, const ValuePtr<Function>&, llvm::Function*);

        ModuleBuilder *m_module_builder;
        IRBuilder m_irbuilder;

        ValuePtr<Function> m_function;
        llvm::Function *m_llvm_function;

        typedef SharedMap<ValuePtr<>, llvm::Value*> ValueTermMap;
        ValueTermMap m_value_terms;

        typedef boost::unordered_map<ValuePtr<Block>, ValueTermMap> BlockMapType;
        BlockMapType m_block_value_terms;
        ValuePtr<Block> m_current_block;

        void run();
        void switch_to_block(const ValuePtr<Block>& block);
        void setup_stack_save_restore(const std::vector<std::pair<ValuePtr<Block>, llvm::BasicBlock*> >&);

        llvm::Value* build_value_instruction(const ValuePtr<Instruction>& term);
        llvm::Value* build_value_functional(const ValuePtr<FunctionalValue>& term);

        llvm::PHINode* build_phi_node(const ValuePtr<>& type, llvm::Instruction *insert_point);
      };
      
      /**
       * Functions for handling simple types.
       */
      ///@{
      llvm::IntegerType* integer_type(llvm::LLVMContext&, const llvm::DataLayout*, IntegerType::Width);
      llvm::Type* float_type(llvm::LLVMContext&, FloatType::Width);
      ///@}

      llvm::TargetMachine* host_machine();

      struct LLVMJitModule {
        ModuleMapping mapping;
        std::size_t load_priority;
      };

      class LLVMJit : public Jit {
      public:
        LLVMJit(const CompileErrorPair& error_loc, const std::string&, const boost::shared_ptr<llvm::TargetMachine>&);
        virtual ~LLVMJit();
        virtual void destroy();

        CompileErrorContext& error_context() {return *m_error_context;}
        virtual void add_module(Module*);
        virtual void remove_module(Module*);
        virtual void* get_symbol(const ValuePtr<Global>&);

      private:
        CompileErrorContext *m_error_context;
        llvm::LLVMContext m_llvm_context;
        llvm::PassManagerBuilder m_llvm_pass_builder;
        llvm::PassManager m_llvm_module_pass;
        llvm::CodeGenOpt::Level m_llvm_opt;
        TargetCallback m_target_callback;
        llvm::Triple m_target_triple;
        boost::shared_ptr<llvm::TargetMachine> m_target_machine;
        std::size_t m_load_priority_max;
        boost::unordered_map<Module*, LLVMJitModule> m_modules;
#if PSI_DEBUG
        boost::shared_ptr<llvm::JITEventListener> m_debug_listener;
#endif
        boost::shared_ptr<llvm::ExecutionEngine> m_llvm_engine;

        void init_llvm_passes();
        void init_llvm_engine(llvm::Module*);
      };
      
      llvm::CallingConv::ID function_call_convention(const CompileErrorPair& error_loc, CallingConvention cc);
      llvm::AttributeSet function_type_attributes(llvm::LLVMContext& ctx, const ValuePtr<FunctionType>& ftype);
    }
  }
}

#endif
