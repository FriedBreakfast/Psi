#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <deque>
#include <exception>
#include <tr1/unordered_map>

#include <llvm/LLVMContext.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/TargetFolder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Value.h>

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

        virtual const llvm::Type* build_type(Term *term);

        /**
         * \brief Return the constant value specified by the given term.
         *
         * \pre <tt>!term->phantom() && term->global()</tt>
         */
        virtual llvm::Constant* build_constant(Term *term) = 0;

        const llvm::APInt& build_constant_integer(Term *term);

        uint64_t type_size(const llvm::Type *ty);
        unsigned type_alignment(const llvm::Type *ty);
	uint64_t constant_type_size(Term *term);
	unsigned constant_type_alignment(Term *term);

      private:
        struct TypeBuilderCallback;

        ConstantBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine);

        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;

        typedef std::tr1::unordered_map<Term*, const llvm::Type*> TypeTermMap;
        TypeTermMap m_type_terms;

        const llvm::Type* build_type_internal(FunctionalTerm *term);
      };

      class ModuleBuilder : public ConstantBuilder {
	friend class ConstantValue;

      public:
        ModuleBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, llvm::Module *llvm_module);
        ~ModuleBuilder();

        virtual llvm::Constant* build_constant(Term *term);
        llvm::GlobalValue* build_global(GlobalTerm *term);
        
        ModuleMapping run(Module*, AggregateLoweringPass::TargetCallback*);

      protected:
        struct ConstantBuilderCallback;
        struct GlobalBuilderCallback;

        Module *m_module;
        llvm::Module *m_llvm_module;

        typedef std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> GlobalTermMap;
        GlobalTermMap m_global_terms;

        typedef std::tr1::unordered_map<Term*, llvm::Constant*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

        llvm::Constant* build_constant_internal(FunctionalTerm *term);
      };

      class FunctionBuilder : public ConstantBuilder {
        friend class ModuleBuilder;

      public:
        typedef std::tr1::unordered_map<Term*, llvm::Value*> ValueTermMap;

        ~FunctionBuilder();

        FunctionTerm *function() {return m_function;}
        llvm::Function* llvm_function() {return m_llvm_function;}
        IRBuilder& irbuilder() {return m_irbuilder;}

        unsigned unknown_alloca_align();

        virtual const llvm::Type* build_type(Term *term);
        virtual llvm::Constant* build_constant(Term *term);
        llvm::Value* build_value(Term *term);

        llvm::StringRef term_name(Term *term);

      private:
        struct ValueBuilderCallback;

        FunctionBuilder(ModuleBuilder*, FunctionTerm*, llvm::Function*);

        ModuleBuilder *m_global_builder;
        IRBuilder m_irbuilder;

        FunctionTerm *m_function;
        llvm::Function *m_llvm_function;

        ValueTermMap m_value_terms;

        void run();
        llvm::BasicBlock* build_function_entry();
        void simplify_stack_save_restore();
        llvm::CallInst* first_stack_restore(llvm::BasicBlock *block);
        bool has_outstanding_alloca(llvm::BasicBlock *block);

        llvm::Instruction* build_value_instruction(InstructionTerm *term);
        llvm::Value* build_value_functional(FunctionalTerm *term);

	llvm::PHINode* build_phi_node(Term *type, llvm::Instruction *insert_point);
      };

      /**
       * Functions for getting LLVM intrinsic functions.
       */
      ///@{
      llvm::Function* intrinsic_memcpy_32(llvm::Module& m);
      llvm::Function* intrinsic_memcpy_64(llvm::Module& m);
      llvm::Function* intrinsic_stacksave(llvm::Module& m);
      llvm::Function* intrinsic_stackrestore(llvm::Module& m);
      ///@}
      
      /**
       * Functions for handling simple types.
       */
      ///@{
      const llvm::IntegerType* integer_type(llvm::LLVMContext&, const llvm::TargetData*, IntegerType::Width);
      const llvm::Type* float_type(llvm::LLVMContext&, FloatType::Width);
      ///@}

      /**
       * Functions for generating the LLVM type of and LLVM values for
       * #Metatype.
       */
      ///@{
      const llvm::Type* metatype_type(llvm::LLVMContext&, const llvm::TargetData*);
      llvm::Constant* metatype_from_constant(ConstantBuilder&, const llvm::APInt& size, const llvm::APInt& align);
      llvm::Constant* metatype_from_constant(ConstantBuilder&, uint64_t size, uint64_t align);
      llvm::Constant* metatype_from_type(ConstantBuilder& builder, const llvm::Type* ty);
      llvm::Value* metatype_from_value(FunctionBuilder&, llvm::Value *size, llvm::Value *align);
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

        llvm::ExecutionEngine *make_engine(llvm::Module*);
      };
    }
  }
}

#endif
