#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <deque>
#include <exception>
#include <tr1/unordered_map>

#include <llvm/LLVMContext.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/TargetFolder.h>
#include <llvm/Target/TargetMachine.h>
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

      class ConstantBuilder;
      class ModuleBuilder;
      class FunctionBuilder;

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
        std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> globals;
      };

      class ConstantBuilder;
      class FunctionBuilder;

      class ConstantBuilder {
        friend class ModuleBuilder;
        friend class FunctionBuilder;

      public:
        ~ConstantBuilder();

        /// \brief Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return *m_llvm_context;}

        /// \brief Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}

        virtual const llvm::Type* build_type(Term *term) = 0;

        /**
         * \brief Return the constant value specified by the given term.
         *
         * \pre <tt>!term->phantom() && term->global()</tt>
         */
        virtual llvm::Constant* build_constant(Term *term) = 0;

        const llvm::APInt& build_constant_integer(Term *term);

      private:
        ConstantBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine);

        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;
      };

      class ModuleBuilder : public ConstantBuilder {
	friend class ConstantValue;

      public:
        ModuleBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, llvm::Module *llvm_module);
        ~ModuleBuilder();

        virtual const llvm::Type* build_type(Term *term);
        virtual llvm::Constant* build_constant(Term *term);
        llvm::GlobalValue* build_global(GlobalTerm *term);
        
        ModuleMapping run(Module*, AggregateLoweringPass::TargetCallback*);
        
        llvm::Function* llvm_memcpy() {return m_llvm_memcpy;}
        llvm::Function* llvm_stacksave() {return m_llvm_stacksave;}
        llvm::Function* llvm_stackrestore() {return m_llvm_stackrestore;}

      protected:
        struct TypeBuilderCallback;
        struct ConstantBuilderCallback;
        struct GlobalBuilderCallback;

        Module *m_module;
        llvm::Module *m_llvm_module;

        typedef std::tr1::unordered_map<Term*, const llvm::Type*> TypeTermMap;
        TypeTermMap m_type_terms;

        const llvm::Type* build_type_internal(FunctionalTerm *term);

        typedef std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> GlobalTermMap;
        GlobalTermMap m_global_terms;

        typedef std::tr1::unordered_map<Term*, llvm::Constant*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

        llvm::Constant* build_constant_internal(FunctionalTerm *term);
        
        llvm::Function *m_llvm_memcpy, *m_llvm_stacksave, *m_llvm_stackrestore;
      };

      class FunctionBuilder {
        friend class ModuleBuilder;

      public:
        typedef std::tr1::unordered_map<Term*, llvm::Value*> ValueTermMap;

        ~FunctionBuilder();

        /// \brief Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return m_global_builder->llvm_context();}

        /// \brief Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_global_builder->llvm_target_machine();}

        FunctionTerm *function() {return m_function;}
        llvm::Function* llvm_function() {return m_llvm_function;}
        IRBuilder& irbuilder() {return m_irbuilder;}

        unsigned unknown_alloca_align();

        const llvm::Type* build_type(Term *term) {return m_global_builder->build_type(term);}
        llvm::Constant* build_constant(Term *term) {return m_global_builder->build_constant(term);}
        llvm::Value* build_value(Term *term);

        llvm::StringRef term_name(Term *term);

        llvm::Function* llvm_memcpy() {return m_global_builder->llvm_memcpy();}
        llvm::Function* llvm_stacksave() {return m_global_builder->llvm_stacksave();}
        llvm::Function* llvm_stackrestore() {return m_global_builder->llvm_stackrestore();}
        
      private:
        struct ValueBuilderCallback;

        FunctionBuilder(ModuleBuilder*, FunctionTerm*, llvm::Function*);

        ModuleBuilder *m_global_builder;
        IRBuilder m_irbuilder;

        FunctionTerm *m_function;
        llvm::Function *m_llvm_function;

        ValueTermMap m_value_terms;

        void run();
        void setup_stack_save_restore(const std::vector<std::pair<BlockTerm*, llvm::BasicBlock*> >&);

        llvm::Value* build_value_instruction(InstructionTerm *term);
        llvm::Value* build_value_functional(FunctionalTerm *term);

	llvm::PHINode* build_phi_node(Term *type, llvm::Instruction *insert_point);
      };

      /**
       * Functions for handling simple types.
       */
      ///@{
      const llvm::IntegerType* integer_type(llvm::LLVMContext&, const llvm::TargetData*, IntegerType::Width);
      const llvm::Type* float_type(llvm::LLVMContext&, FloatType::Width);
      ///@}

      llvm::TargetMachine* host_machine();

      boost::shared_ptr<AggregateLoweringPass::TargetCallback> create_target_fixes(llvm::LLVMContext*, const boost::shared_ptr<llvm::TargetMachine>&, const std::string&);

      class LLVMJit : public Jit {
      public:
        LLVMJit(const boost::shared_ptr<JitFactory>&, const std::string&, const boost::shared_ptr<llvm::TargetMachine>&);
        virtual ~LLVMJit();

        virtual void add_module(Module*);
        virtual void remove_module(Module*);
        virtual void rebuild_module(Module*, bool);
        virtual void* get_symbol(GlobalTerm*);

      private:
        llvm::LLVMContext m_llvm_context;
        boost::shared_ptr<AggregateLoweringPass::TargetCallback> m_target_fixes;
        boost::shared_ptr<llvm::TargetMachine> m_target_machine;
        std::tr1::unordered_map<Module*, ModuleMapping> m_modules;
#ifdef PSI_DEBUG
        boost::shared_ptr<llvm::JITEventListener> m_debug_listener;
#endif
        boost::shared_ptr<llvm::ExecutionEngine> m_llvm_engine;

        void init_llvm_engine(llvm::Module*);
      };
    }
  }
}

#endif
