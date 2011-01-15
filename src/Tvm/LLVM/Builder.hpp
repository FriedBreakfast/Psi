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

#include "../Core.hpp"
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

      class ConstantBuilder;
      class FunctionBuilder;

      /**
       * The inner workings of LLVM mean that it violates the ABI on
       * some targets (for example, <tt>{i8,i8}</tt> will not be
       * returned correctly on x86 and x86-64). This has to be worked
       * around at the IR level, and this class is where to put
       * machine-specific fixes to make LLVM work.
       *
       * Note that these only apply to C-style calling conventions,
       * the internal calling convention just uses pointers and it is
       * assumed these work correctly by default.
       */
      struct TargetFixes {
#if 0
        /**
         * Map a function type into LLVM.
         */
        virtual const llvm::FunctionType* function_type(ConstantBuilder& builder, FunctionTypeTerm *term) = 0;

        /**
         * Set up a function call.
         */
        virtual llvm::Instruction* function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn) = 0;

        /**
         * Unpack the parameters passed to a function into a friendly
         * form.
         */
        virtual void function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function,
                                                llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result) = 0;

        /**
         * Create a function return.
         */
        virtual void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, llvm::Function *llvm_function, Term *value) = 0;
#endif
      };

      class ConstantBuilder {
        friend class ModuleBuilder;
        friend class FunctionBuilder;

      public:
        ~ConstantBuilder();

        /// \brief Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return *m_llvm_context;}

        /// \brief Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}

        /// \brief Get the target-specific set of LLVM fixes.
        TargetFixes* target_fixes() {return m_target_fixes;}

        virtual const llvm::Type* build_type(Term *term);

	const llvm::Type* get_float_type(FloatType::Width);
	const llvm::IntegerType* get_boolean_type();
	const llvm::IntegerType* get_integer_type(IntegerType::Width);
	const llvm::Type* get_byte_type();
	const llvm::Type* get_pointer_type();
        const llvm::IntegerType *get_intptr_type();
        unsigned intptr_type_bits();

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

        ConstantBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes);

        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;
        TargetFixes *m_target_fixes;

        typedef std::tr1::unordered_map<Term*, const llvm::Type*> TypeTermMap;
        TypeTermMap m_type_terms;

        const llvm::Type* build_type_internal(FunctionalTerm *term);
        const llvm::Type* build_type_internal_simple(FunctionalTerm *term);
      };

      class ModuleBuilder : public ConstantBuilder {
	friend class ConstantValue;

      public:
        ModuleBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes, llvm::Module *module=0);
        ~ModuleBuilder();

        void set_module(llvm::Module *module);

        llvm::Module& llvm_module() {return *m_module;}

        virtual llvm::Constant* build_constant(Term *term);
        llvm::GlobalValue* build_global(GlobalTerm *term);

      protected:
        struct ConstantBuilderCallback;
        struct GlobalBuilderCallback;

        llvm::Module *m_module;

        /// Global terms which have been encountered but not yet built
        typedef std::deque<std::pair<GlobalTerm*, llvm::GlobalValue*> > GlobalTermBuildList;
        GlobalTermBuildList m_global_build_list;

        typedef std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> GlobalTermMap;
        GlobalTermMap m_global_terms;

        typedef std::tr1::unordered_map<Term*, llvm::Constant*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

        llvm::Constant* build_constant_internal(FunctionalTerm *term);
        llvm::Constant* build_constant_internal_simple(FunctionalTerm *term);
      };

      class FunctionBuilder : public ConstantBuilder {
        friend class ModuleBuilder;

      public:
        typedef std::tr1::unordered_map<Term*, llvm::Value*> ValueTermMap;

        ~FunctionBuilder();

        llvm::Module& llvm_module() {return m_global_builder->llvm_module();}

        FunctionTerm *function() {return m_function;}
        llvm::Function* llvm_function() {return m_llvm_function;}
        IRBuilder& irbuilder() {return *m_irbuilder;}

        unsigned unknown_alloca_align();

        virtual const llvm::Type* build_type(Term *term);
        virtual llvm::Constant* build_constant(Term *term);
        llvm::Value* build_value(Term *term);

        llvm::Value* cast_pointer_to_generic(llvm::Value *value);
        llvm::Value* cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type);

        llvm::StringRef term_name(Term *term);

      private:
        struct ValueBuilderCallback;

        FunctionBuilder(ModuleBuilder *global_builder, FunctionTerm *function,
                        llvm::Function *llvm_function, IRBuilder *irbuilder);

        ModuleBuilder *m_global_builder;
        IRBuilder *m_irbuilder;

        FunctionTerm *m_function;
        llvm::Function *m_llvm_function;

        ValueTermMap m_value_terms;

        void run();
        llvm::BasicBlock* build_function_entry();
        void simplify_stack_save_restore();
        llvm::CallInst* first_stack_restore(llvm::BasicBlock *block);
        bool has_outstanding_alloca(llvm::BasicBlock *block);

        llvm::Instruction* build_value_instruction(InstructionTerm *term);
        llvm::Instruction* build_value_instruction_simple(InstructionTerm *term);
        llvm::Value* build_value_functional(FunctionalTerm *term);
        llvm::Value* build_value_functional_simple(FunctionalTerm *term);

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
       * Functions for generating the LLVM type of and LLVM values for
       * #Metatype.
       */
      ///@{
      const llvm::Type* metatype_type(ConstantBuilder&);
      llvm::Constant* metatype_from_constant(ConstantBuilder&, const llvm::APInt& size, const llvm::APInt& align);
      llvm::Constant* metatype_from_constant(ConstantBuilder&, uint64_t size, uint64_t align);
      llvm::Constant* metatype_from_type(ConstantBuilder& builder, const llvm::Type* ty);
      llvm::Value* metatype_from_value(FunctionBuilder&, llvm::Value *size, llvm::Value *align);
      ///@}

      llvm::TargetMachine* host_machine();

      boost::shared_ptr<TargetFixes> create_target_fixes(const std::string& triple);

      class LLVMJit : public Jit {
      public:
        LLVMJit(const boost::shared_ptr<JitFactory>&, const std::string&, llvm::TargetMachine*);
        virtual ~LLVMJit();

        virtual void add_module(Module*);
        virtual void remove_module(Module*);
        virtual void rebuild_module(Module*, bool);
        virtual void* get_symbol(GlobalTerm*);

        void register_llvm_jit_listener(llvm::JITEventListener *l);
        void unregister_llvm_jit_listener(llvm::JITEventListener *l);

      private:
        llvm::LLVMContext m_llvm_context;
        boost::shared_ptr<TargetFixes> m_target_fixes;
        ModuleBuilder m_builder;
        boost::shared_ptr<llvm::ExecutionEngine> m_llvm_engine;

        static llvm::ExecutionEngine *make_engine(llvm::LLVMContext& context);
      };
    }
  }
}

#endif
