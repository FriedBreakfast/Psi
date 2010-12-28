#ifndef HPP_PSI_TVM_LLVM_BUILDER
#define HPP_PSI_TVM_LLVM_BUILDER

#include <deque>
#include <exception>
#include <tr1/unordered_map>

#include <boost/intrusive/list.hpp>
#include <boost/optional.hpp>
#include <boost/pool/object_pool.hpp>
#include <boost/shared_array.hpp>

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
#include "../Number.hpp"
#include "../../Utility.hpp"

namespace Psi {
  namespace Tvm {
    namespace LLVM {
      typedef llvm::IRBuilder<true, llvm::TargetFolder, llvm::IRBuilderDefaultInserter<true> > IRBuilder;

      class ConstantBuilder;
      class GlobalBuilder;
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

      class BuiltValue : public boost::intrusive::list_base_hook<> {
	friend class GlobalBuilder;
	friend class FunctionBuilder;

      public:
        enum State {
          /// A union value - LLVM cannot represent these
          state_union,
          /// A sequence (struct or array) value - LLVM can represent
          /// these but not if they contain union or unknown
          /// values. In this case, they are represented as sequences
          /// here and LLVM only sees the members. Note that this only
          /// applies when the number of elements in the sequence is
          /// known; otherwise the unknown type applies.
          state_sequence,
          /// A value with a direct LLVM mapping.
          state_simple,
          /// A value which is just a black box of data, with a size
          /// and an alignment.
          state_unknown
        };

      private:
	Term *m_type;
	State m_state;
	const llvm::Type *m_simple_type;
	llvm::SmallVector<BuiltValue*, 4> m_elements;

      public:
        /**
         * The term type this value represents.
         */
	Term *type() const {return m_type;}

        /**
         * Which subclass of BuiltValue this is in fact an
         * instance of.
         */
        State state() const {return m_state;}

        /**
         * If this value has a simple LLVM type, this gives that type.
         */
        const llvm::Type *simple_type() const {return m_simple_type;}

	/**
	 * Return a simple value corresponding to this value.
	 */
	virtual llvm::Value *simple_value() const = 0;

	/**
	 * Return a value which is a pointer to the encoded data for
	 * this value.
	 */
	virtual llvm::Value *raw_value() const = 0;

      protected:
        BuiltValue(ConstantBuilder&, Term*);
      };

      class ConstantValue : public BuiltValue {
	friend class GlobalBuilder;

      public:
	virtual llvm::Constant *simple_value() const;
	virtual llvm::Constant *raw_value() const;

      private:
	GlobalBuilder *m_builder;
	llvm::Constant *m_simple_value;
	llvm::SmallVector<char, 0> m_raw_value;

	ConstantValue(GlobalBuilder *builder, Term *type);
      };

      class FunctionValue : public BuiltValue {
	friend class FunctionBuilder;

      public:
	/**
	 * Where this value was created. When this value is converted
	 * to other types, this is the point where the conversion
	 * instructions are inserted.
	 */
	llvm::Instruction *origin() {return m_origin;}

	virtual llvm::Value *simple_value() const;
	virtual llvm::Value *raw_value() const;

      private:
	FunctionBuilder *m_builder;
	llvm::Instruction *m_origin;
	llvm::Value *m_simple_value;
	llvm::Value *m_raw_value;
	llvm::SmallVector<BuiltValue*, 4> m_elements;

	FunctionValue(FunctionBuilder *builder, Term *type, llvm::Instruction *origin);
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
        virtual void function_return(FunctionBuilder& builder, FunctionTypeTerm *function_type, llvm::Function *llvm_function, Term *value) const = 0;
      };

      class ConstantBuilder {
        friend class GlobalBuilder;
        friend class FunctionBuilder;

      public:
        ~ConstantBuilder();

	Context& context() {return *m_context;}

        /// \brief Get the LLVM context used to create IR.
        llvm::LLVMContext& llvm_context() {return *m_llvm_context;}

        /// \brief Get the llvm::TargetMachine we're building IR for.
        llvm::TargetMachine* llvm_target_machine() {return m_llvm_target_machine;}

        /// \brief Get the target-specific set of LLVM fixes.
        TargetFixes* target_fixes() {return m_target_fixes;}

        virtual const llvm::Type* build_type(Term *term);

	const llvm::Type* get_float_type(FloatType::Width);
	const llvm::IntegerType* get_integer_type(IntegerType::Width);

        /**
         * \brief Return the constant value specified by the given term.
         *
         * \pre <tt>!term->phantom() && term->global()</tt>
         */
        virtual ConstantValue* build_constant(Term *term) = 0;

        llvm::Constant* build_constant_simple(Term *term);
        const llvm::APInt& build_constant_integer(Term *term);

        const llvm::IntegerType *intptr_type();
        unsigned intptr_type_bits();
        uint64_t type_size(const llvm::Type *ty);
        uint64_t type_alignment(const llvm::Type *ty);

	/**
	 * Convert a BuiltValue to an LLVM value. The value should
	 * have a simple type.
	 */
	virtual llvm::Value* value_to_llvm(BuiltValue *value) = 0;

	/**
	 * Get a BuiltValue for an element of an existing value at a
	 * fixed index. This works for struct, array and union types.
	 */
	virtual BuiltValue* get_element_value(BuiltValue *value, unsigned index) = 0;

	/**
	 * Get the unique value of the empty type.
	 */
        BuiltValue* empty_value() {return m_empty_value;}

      private:
        struct TypeBuilderCallback;

        ConstantBuilder(Context *context, llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes);

	Context *m_context;
        llvm::LLVMContext *m_llvm_context;
        llvm::TargetMachine *m_llvm_target_machine;
        TargetFixes *m_target_fixes;
	BuiltValue *m_empty_value;

        typedef std::tr1::unordered_map<Term*, boost::optional<const llvm::Type*> > TypeTermMap;
        TypeTermMap m_type_terms;

        const llvm::Type* build_type_internal(FunctionalTerm *term);
        const llvm::Type* build_type_internal_simple(FunctionalTerm *term);
      };

      class GlobalBuilder : public ConstantBuilder {
      public:
        GlobalBuilder(Context *context, llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes, llvm::Module *module=0);
        ~GlobalBuilder();

        void set_module(llvm::Module *module);

        llvm::Module& llvm_module() {return *m_module;}

        virtual ConstantValue* build_constant(Term *term);
        llvm::GlobalValue* build_global(GlobalTerm *term);

	virtual llvm::Value* value_to_llvm(BuiltValue *value);
	virtual BuiltValue* get_element_value(BuiltValue *value, unsigned index);

	///@{
	/**
	 * Functions for creating new ConstantValue objects.
	 */

	ConstantValue* new_constant_value_simple(Term *type, llvm::Constant *value);
	ConstantValue* new_constant_value_raw(Term *type, const llvm::SmallVectorImpl<char>& data);
	ConstantValue* new_constant_value_aggregate(Term *type, const llvm::SmallVectorImpl<ConstantValue*>& elements);
	///@}

      protected:
        struct GlobalBuilderCallback;
        struct ConstantBuilderCallback;

        llvm::Module *m_module;

        /// Global terms which have been encountered but not yet built
        typedef std::deque<std::pair<GlobalTerm*, llvm::GlobalValue*> > GlobalTermBuildList;
        GlobalTermBuildList m_global_build_list;

        typedef std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*> GlobalTermMap;
        GlobalTermMap m_global_terms;

        typedef std::tr1::unordered_map<Term*, ConstantValue*> ConstantTermMap;
        ConstantTermMap m_constant_terms;

	boost::object_pool<ConstantValue> m_constant_value_pool;
	ConstantValue* new_constant_value(Term *type);

        ConstantValue* build_constant_internal(FunctionalTerm *term);
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

        unsigned unknown_alloca_align();

	llvm::Instruction* insert_placeholder_instruction();

        virtual const llvm::Type* build_type(Term *term);
        virtual ConstantValue* build_constant(Term *term);
        BuiltValue* build_value(Term *term);
        llvm::Value* build_value_simple(Term *term);

	///@{
	/**
	 * Functions for creating new FunctionValue objects.
	 *
	 * \param origin Instruction where the specified value was
	 * created. If this is NULL, a placeholder instruction will be
	 * inserted at the current position.
	 */

	FunctionValue* new_function_value_simple(Term *type, llvm::Value *value, llvm::Instruction *origin=0);
	FunctionValue* new_function_value_raw(Term *type, llvm::Value *ptr, llvm::Instruction *origin=0);
	FunctionValue* new_function_value_aggregate(Term *type, const llvm::SmallVectorImpl<BuiltValue*>& elements, llvm::Instruction *origin=0);
	///@}

	void store_value(BuiltValue *value, llvm::Value *ptr);
	BuiltValue* load_value(Term *type, llvm::Value *ptr);
	virtual llvm::Value* value_to_llvm(BuiltValue *value);
	virtual BuiltValue* get_element_value(BuiltValue *value, unsigned index);

        llvm::Value* cast_pointer_to_generic(llvm::Value *value);
        llvm::Value* cast_pointer_from_generic(llvm::Value *value, const llvm::Type *type);
	llvm::Instruction* create_memcpy(llvm::Value *dest, llvm::Value *src, llvm::Value *count);

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

	boost::object_pool<FunctionValue> m_function_value_pool;
	FunctionValue* new_function_value(Term *type, llvm::Instruction *origin);

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

	BuiltValue* build_phi_node(Term *type, llvm::Instruction *insert_point);
	void populate_phi_node(BuiltValue *phi_node, llvm::BasicBlock *incoming_block, BuiltValue *value);
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

      boost::shared_ptr<TargetFixes> create_target_fixes(const std::string& triple);

      class LLVMJit : public Jit {
      public:
        LLVMJit(Context *context, const std::string& host_triple, llvm::TargetMachine *host_machine);
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
