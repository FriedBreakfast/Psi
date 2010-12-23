#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <deque>
#include <stdexcept>
#include <tr1/unordered_map>

#include <boost/optional.hpp>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/LLVMContext.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/TargetFolder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Value.h>

#include "../Core.hpp"
#include "../Jit.hpp"
#include "../Function.hpp"
#include "../Functional.hpp"
#include "../../Utility.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      typedef llvm::IRBuilder<true, llvm::TargetFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

      class FunctionBuilder;

      /**
       * Thrown when an error occurs during LLVM construction: many of
       * these use PSI_ASSERT, but this can also be used when the error
       * condition has not been tested well enough.
       */
      class BuildError : public std::logic_error {
      public:
        explicit BuildError(const std::string& message);
      };

      /**
       * Class which represents the various different types of LLVM
       * value understood by Tvm. This distinguishes between known
       * values (which have an exact or close-enough LLVM
       * representation) and unknown values which must be stored on the
       * stack using \c alloca.
       */
      class BuiltValue {
        enum Category {
          /// \brief Invalid - used for default constructed objects
          category_invalid,
          /// \brief Known - where a complete LLVM type mapping is
          /// available.
          category_known,
          /// \brief Unknown - where the size and alignment are only
          /// known at runtime.
          category_unknown
        };

        friend BuiltValue value_known(llvm::Value*);
        friend BuiltValue value_unknown(llvm::Value*);

        BuiltValue(Category category, llvm::Value *value) : m_category(category), m_value(value) {}

        Category m_category;
        llvm::Value *m_value;

      public:
        BuiltValue() : m_category(category_invalid), m_value(0) {}

        bool valid() const {return m_category != category_invalid;}
        bool known() const {return m_category == category_known;}
        bool unknown() const {return m_category == category_unknown;}
        llvm::Value *known_value() const {PSI_ASSERT(known()); return m_value;}
        llvm::Value *unknown_value() const {PSI_ASSERT(unknown()); return m_value;}
      };

      inline BuiltValue value_known(llvm::Value *value) {
        return BuiltValue(BuiltValue::category_known, value);
      }

      inline BuiltValue value_unknown(llvm::Value *value) {
        return BuiltValue(BuiltValue::category_unknown, value);
      }

      class ConstantBuilder {
        friend class GlobalBuilder;
        friend class FunctionBuilder;

      public:
        ~ConstantBuilder();

        /// Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return *m_llvm_context;}

        /// Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}

        virtual const llvm::Type* build_type(Term *term);

        /**
         * \brief Return the constant value specified by the given term.
         *
         * \pre <tt>!term->phantom() && term->global()</tt>
         */
        virtual llvm::Constant* build_constant(Term *term) = 0;

        const llvm::APInt& build_constant_integer(Term *term);

        const llvm::IntegerType *intptr_type();
        unsigned intptr_type_bits();
        uint64_t type_size(const llvm::Type *ty);
        uint64_t type_alignment(const llvm::Type *ty);

      private:
        struct TypeBuilderCallback;

        ConstantBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine);

        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;

        typedef std::tr1::unordered_map<Term*, boost::optional<const llvm::Type*> > TypeTermMap;
        TypeTermMap m_type_terms;

        const llvm::Type* build_type_internal(FunctionalTerm *term);
      };

      class GlobalBuilder : public ConstantBuilder {
      public:
        GlobalBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine, llvm::Module *module=0);
        ~GlobalBuilder();

        void set_module(llvm::Module *module);

        llvm::Module& llvm_module() {return *m_module;}

        virtual llvm::Constant* build_constant(Term *term);
        llvm::GlobalValue* build_global(GlobalTerm *term);

      protected:
        struct GlobalBuilderCallback;
        struct ConstantBuilderCallback;

        llvm::Module *m_module;

        /// Global terms which have been encountered but not yet built
        typedef std::deque<std::pair<GlobalTerm*, llvm::GlobalValue*> > GlobalTermBuildList;
        GlobalTermBuildList m_global_build_list;

        typedef std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> GlobalTermMap;
        GlobalTermMap m_global_terms;

        typedef std::tr1::unordered_map<Term*, llvm::Constant*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

        std::pair<llvm::Constant*, uint64_t> build_constant_internal(FunctionalTerm *term);
        llvm::Constant* build_constant_known_internal(FunctionalTerm *term);
        const llvm::Type* build_constant_type(FunctionalTerm *term);
      };

      class FunctionBuilder : public ConstantBuilder {
        friend class GlobalBuilder;

      public:
        typedef std::tr1::unordered_map<Term*, BuiltValue> ValueTermMap;

        ~FunctionBuilder();

        llvm::Module& llvm_module() {return m_global_builder->llvm_module();}

        FunctionTerm *function() {return m_function;}
        llvm::Function* llvm_function() {return m_llvm_function;}
        IRBuilder& irbuilder() {return *m_irbuilder;}

        /// Returns the maximum alignment for any type supported. This
        /// seems to have to be hardwired which is bad, but 8 should be
        /// enough for all current platforms.
        unsigned unknown_alloca_align() const {return 8;}

        virtual const llvm::Type* build_type(Term *term);
        virtual llvm::Constant* build_constant(Term *term);
        BuiltValue build_value(Term *term);
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
        struct ValueBuilderCallback;

        FunctionBuilder(GlobalBuilder *global_builder, FunctionTerm *function,
                        llvm::Function *llvm_function, IRBuilder *irbuilder);

        GlobalBuilder *m_global_builder;
        IRBuilder *m_irbuilder;

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

        BuiltValue build_value_instruction(InstructionTerm *term);
        llvm::Value* build_value_known_instruction(InstructionTerm *term);
        BuiltValue build_value_functional(FunctionalTerm *term);
        llvm::Value* build_value_known_functional(FunctionalTerm *term);
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
      BuiltValue metatype_from_value(FunctionBuilder&, llvm::Value *size, llvm::Value *align);

      const llvm::APInt& metatype_constant_size(llvm::Constant *value);
      const llvm::APInt& metatype_constant_align(llvm::Constant *value);
      llvm::Value* metatype_value_size(FunctionBuilder&, llvm::Value*);
      llvm::Value* metatype_value_align(FunctionBuilder&, llvm::Value*);
      ///@}

      llvm::Value* empty_value(ConstantBuilder&);

      llvm::TargetMachine* host_machine();

      class LLVMJit : public Jit {
      public:
        LLVMJit(Context *context);
        virtual ~LLVMJit();

        virtual void* get_global(GlobalTerm *global);

        void register_llvm_jit_listener(llvm::JITEventListener *l);
        void unregister_llvm_jit_listener(llvm::JITEventListener *l);

      private:
        Context *m_context;
        llvm::LLVMContext m_llvm_context;
        LLVM::GlobalBuilder m_builder;
        boost::shared_ptr<llvm::ExecutionEngine> m_llvm_engine;

        static llvm::ExecutionEngine *make_engine(llvm::LLVMContext& context);
      };
    }
  }
}

#endif
