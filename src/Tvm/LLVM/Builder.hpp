#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <deque>
#include <stdexcept>
#include <tr1/unordered_map>

#include <boost/intrusive/list.hpp>
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
#include "../Instructions.hpp"
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

      class BuiltValue : public boost::intrusive::list_base_hook<> {
        friend class ConstantBuilder;

      public:
        enum State {
          /// BuiltUnionValue: A union value - LLVM cannot represent these
          state_union,
          /// BuiltValueSequence: A sequence (struct or array) value -
          /// LLVM can represent these but not if they contain union
          /// or unknown values. In this case, they are represented as
          /// sequences here and LLVM only sees the members. Note that
          /// this only applies when the number of elements in the
          /// sequence is known; otherwise the unknown type applies.
          state_sequence,
          /// BuiltValueSimple: A value with a direct LLVM mapping.
          state_simple,
          /// BuiltValueUnknown: A value which is just a black box of
          /// data, with a size and an alignment.
          state_unknown
        };

        /**
         * The term type this value represents.
         */
        Term *type;

        /**
         * Which subclass of BuiltValue this is in fact an
         * instance of.
         */
        State state;

        /**
         * If this value has a simple LLVM type, this gives that type.
         */
        const llvm::Type *simple_type;

        /**
         * Exact LLVM equivalent, if available. This requires both
         * that there is an exact LLVM equivalent of the type of this
         * term, and that it has been loaded from the stack if it was
         * created there.
         */
        llvm::Value *simple_value;

        /**
         * If this value is on the stack for one reason or another,
         * this stores the pointer to the raw data. In this case,
         * inner members may not be initialized.
         *
         * For constant values, rather than being an <tt>i8*</tt>,
         * this should be an <tt>[i8 x n]</tt>; in either case this
         * means the type subclasses llvm::SequentialType.
         */
        llvm::Value *raw_value;

        /**
         * Elements of this type, if it is an aggregate. The size of
         * this array is always exactly equal to the number of
         * elements in the aggregate.
         *
         * For unions, it each element of the same type should be
         * filled in at the same time, so that all elements of the
         * same type are treated equivalently.
         */
        llvm::SmallVector<BuiltValue*, 4> elements;

      private:
        BuiltValue(Term *type_, State state_)
          : type(type_), state(state_), simple_type(0),
            simple_value(0), raw_value(0) {}
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
        /**
         * Map a function type into LLVM.
         */
        virtual const llvm::FunctionType* function_type(ConstantBuilder& builder, FunctionTypeTerm *term) const = 0;

        /**
         * Set up a function call.
         */
        virtual BuiltValue* function_call(FunctionBuilder& builder, llvm::Value *target, FunctionTypeTerm *target_type, FunctionCall::Ptr insn) const = 0;

        /**
         * Unpack the parameters passed to a function into a friendly
         * form.
         */
        virtual void function_parameters_unpack(FunctionBuilder& builder, FunctionTerm *function,
                                                llvm::Function *llvm_function, llvm::SmallVectorImpl<BuiltValue*>& result) const = 0;

        /**
         * Create a function return.
         */
        virtual void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, Term *value) const = 0;
      };

      class ConstantBuilder {
        friend class GlobalBuilder;
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

        /**
         * \brief Return the constant value specified by the given term.
         *
         * \pre <tt>!term->phantom() && term->global()</tt>
         */
        virtual BuiltValue* build_constant(Term *term) = 0;

        llvm::Constant* build_constant_simple(Term *term);
        const llvm::APInt& build_constant_integer(Term *term);

        const llvm::IntegerType *intptr_type();
        unsigned intptr_type_bits();
        uint64_t type_size(const llvm::Type *ty);
        uint64_t type_alignment(const llvm::Type *ty);

        BuiltValue* new_value(Term *type);
        BuiltValue* new_value_simple(Term *type, llvm::Value *value);
        BuiltValue* empty_value();

      private:
        struct TypeBuilderCallback;

        ConstantBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes);

        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;
        TargetFixes *m_target_fixes;

        typedef std::tr1::unordered_map<Term*, boost::optional<const llvm::Type*> > TypeTermMap;
        TypeTermMap m_type_terms;

        typedef boost::intrusive::list<BuiltValue,
                                       boost::intrusive::constant_time_size<false> > BuiltValueListType;
        BuiltValueListType m_built_values;

        const llvm::Type* build_type_internal(FunctionalTerm *term);
        const llvm::Type* build_type_internal_simple(FunctionalTerm *term);
      };

      class GlobalBuilder : public ConstantBuilder {
      public:
        GlobalBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes, llvm::Module *module=0);
        ~GlobalBuilder();

        void set_module(llvm::Module *module);

        llvm::Module& llvm_module() {return *m_module;}

        virtual BuiltValue* build_constant(Term *term);
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

        typedef std::tr1::unordered_map<Term*, BuiltValue*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

        BuiltValue* build_constant_internal(FunctionalTerm *term);
        llvm::Constant* build_constant_internal_simple(FunctionalTerm *term);
        const llvm::Type* build_constant_type(FunctionalTerm *term);
      };

      class FunctionBuilder : public ConstantBuilder {
        friend class GlobalBuilder;

      public:
        typedef std::tr1::unordered_map<Term*, BuiltValue*> ValueTermMap;

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
        virtual BuiltValue* build_constant(Term *term);
        BuiltValue* build_value(Term *term);
        llvm::Value* build_value_simple(Term *term);

        llvm::Value* cast_pointer_to_generic(llvm::Value *value);
        llvm::Value* cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type);

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

        BuiltValue* build_value_instruction(InstructionTerm *term);
        llvm::Value* build_value_instruction_simple(InstructionTerm *term);
        BuiltValue* build_value_functional(FunctionalTerm *term);
        llvm::Value* build_value_functional_simple(FunctionalTerm *term);
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

      const llvm::APInt& metatype_constant_size(llvm::Constant *value);
      const llvm::APInt& metatype_constant_align(llvm::Constant *value);
      llvm::Value* metatype_value_size(FunctionBuilder&, llvm::Value*);
      llvm::Value* metatype_value_align(FunctionBuilder&, llvm::Value*);
      ///@}

      llvm::Value* empty_value(ConstantBuilder&);

      llvm::TargetMachine* host_machine();

      boost::shared_ptr<TargetFixes> create_target_fixes(const llvm::Target& target);

      class LLVMJit : public Jit {
      public:
        LLVMJit(Context *context, llvm::TargetMachine *host_machine);
        virtual ~LLVMJit();

        virtual void* get_global(GlobalTerm *global);

        void register_llvm_jit_listener(llvm::JITEventListener *l);
        void unregister_llvm_jit_listener(llvm::JITEventListener *l);

      private:
        Context *m_context;
        llvm::LLVMContext m_llvm_context;
        boost::shared_ptr<TargetFixes> m_target_fixes;
        LLVM::GlobalBuilder m_builder;
        boost::shared_ptr<llvm::ExecutionEngine> m_llvm_engine;

        static llvm::ExecutionEngine *make_engine(llvm::LLVMContext& context);
      };
    }
  }
}

#endif
