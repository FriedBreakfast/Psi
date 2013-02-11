#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <limits>
#include <deque>
#include <exception>
#include <boost/unordered_map.hpp>

#include <llvm/LLVMContext.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IRBuilder.h>
#include <llvm/PassManager.h>
#include <llvm/Support/TargetFolder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Value.h>

#ifdef PSI_DEBUG
#include <llvm/ExecutionEngine/JITEventListener.h>
#endif

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

      /**
       * Thrown when an error occurs during LLVM construction: many of
       * these use PSI_ASSERT, but this can also be used when the error
       * condition has not been tested well enough.
       */
      class BuildError : public std::exception {
      public:
        explicit BuildError(const std::string& message);
        virtual ~BuildError() throw ();
        virtual const char* what() const throw();

      private:
        const char *m_str;
        std::string m_message;
      };
      
      struct ModuleMapping {
        llvm::Module *module;
        boost::unordered_map<ValuePtr<Global>, llvm::GlobalValue*> globals;
      };

      class TargetCallback {
      public:
        /**
         * Get a callback class for use by the aggregate lowering pass.
         */
        virtual AggregateLoweringPass::TargetCallback* aggregate_lowering_callback() = 0;
        
        /**
         * \brief Set up or get the exception personality routine with the specified name.
         * 
         * \param module Module to set up the handler for.
         * 
         * \param basename Name of the personality to use. Interpretation of this name is
         * platform specific.
         */
        virtual llvm::Function* exception_personality_routine(llvm::Module *module, const std::string& basename) = 0;
      };

      class ModuleBuilder {
      public:
        ModuleBuilder(llvm::LLVMContext*, llvm::TargetMachine*, llvm::Module*, llvm::FunctionPassManager*, TargetCallback*);
        ~ModuleBuilder();

        /// \brief Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return *m_llvm_context;}

        /// \brief Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}
        
        TargetCallback *target_callback() {return m_target_callback;}

        virtual llvm::Type* build_type(const ValuePtr<>& term);
        virtual llvm::Constant* build_constant(const ValuePtr<>& term);
        llvm::GlobalValue* build_global(const ValuePtr<Global>& term);

        const llvm::APInt& build_constant_integer(const ValuePtr<>& term);
        
        ModuleMapping run(Module*);
        
        llvm::Module* llvm_module() {return m_llvm_module;}
        
        llvm::Function* llvm_memcpy() {return m_llvm_memcpy;}
        llvm::Function* llvm_memset() {return m_llvm_memset;}
        llvm::Function* llvm_stacksave() {return m_llvm_stacksave;}
        llvm::Function* llvm_stackrestore() {return m_llvm_stackrestore;}
        llvm::Function* llvm_eh_exception() {return m_llvm_eh_exception;}
        llvm::Function* llvm_eh_selector() {return m_llvm_eh_selector;}
        llvm::Function* llvm_eh_typeid_for() {return m_llvm_eh_typeid_for;}

      protected:
        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;
        llvm::FunctionPassManager *m_llvm_function_pass;
        Module *m_module;
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
        *m_llvm_eh_exception, *m_llvm_eh_selector, *m_llvm_eh_typeid_for;
      };

      class FunctionBuilder {
        friend class ModuleBuilder;

      public:
        ~FunctionBuilder();
        
        ModuleBuilder *module_builder() {return m_module_builder;}

        const ValuePtr<Function>& function() {return m_function;}
        llvm::Function* llvm_function() {return m_llvm_function;}
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

      boost::shared_ptr<TargetCallback> create_target_fixes(llvm::LLVMContext*, const boost::shared_ptr<llvm::TargetMachine>&, const std::string&);

      class LLVMJit : public Jit {
      public:
        LLVMJit(const boost::shared_ptr<JitFactory>&, const std::string&, const boost::shared_ptr<llvm::TargetMachine>&);
        virtual ~LLVMJit();

        virtual void add_module(Module*);
        virtual void remove_module(Module*);
        virtual void rebuild_module(Module*, bool);
        virtual void add_or_rebuild_module(Module *module, bool incremental);
        virtual void* get_symbol(const ValuePtr<Global>&);

      private:
        llvm::LLVMContext m_llvm_context;
        llvm::PassManagerBuilder m_llvm_pass_builder;
        llvm::PassManager m_llvm_module_pass;
        llvm::CodeGenOpt::Level m_llvm_opt;
        boost::shared_ptr<TargetCallback> m_target_fixes;
        boost::shared_ptr<llvm::TargetMachine> m_target_machine;
        boost::unordered_map<Module*, ModuleMapping> m_modules;
#ifdef PSI_DEBUG
        boost::shared_ptr<llvm::JITEventListener> m_debug_listener;
#endif
        boost::shared_ptr<llvm::ExecutionEngine> m_llvm_engine;

        void init_llvm_passes();
        void init_llvm_engine(llvm::Module*);
      };
    }
  }
}

#endif
