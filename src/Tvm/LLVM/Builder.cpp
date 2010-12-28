#include "Builder.hpp"
#include "Templates.hpp"

#include "../Aggregate.hpp"
#include "../Core.hpp"
#include "../Function.hpp"
#include "../Functional.hpp"
#include "../Recursive.hpp"

#include <boost/make_shared.hpp>

#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/System/Host.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetRegistry.h>
#include <llvm/Target/TargetSelect.h>

/*
 * Do not remove the JIT.h include. Although everything will build
 * fine, the JIT will not be available since JIT.h includes some magic
 * which ensures the JIT is really available.
 */
#include <llvm/ExecutionEngine/JIT.h>

namespace Psi {
  namespace Tvm {
    /**
     * Namespace containing classes to convert Tvm code to the LLVM
     * IR. The generated code will be machine-specific.
     */
    namespace LLVM {
      template<typename T>
      struct PtrValidBase {
        T* invalid() const {return NULL;}
        bool valid(const T* t) const {return t;}
      };

      BuildError::BuildError(const std::string& msg) {
	m_message = "LLVM IR generation error: ";
	m_message += msg;
	m_str = m_message.c_str();
      }

      BuildError::~BuildError() throw () {
      }

      const char* BuildError::what() const throw() {
	return m_str;
      }

      struct ConstantBuilder::TypeBuilderCallback {
        ConstantBuilder *self;
        TypeBuilderCallback(ConstantBuilder *self_) : self(self_) {}

        const llvm::Type* build(Term* term) const {
          switch(term->term_type()) {
          case term_functional:
            return self->build_type_internal(cast<FunctionalTerm>(term));

          case term_apply: {
            Term* actual = cast<ApplyTerm>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            return self->build_type(actual);
          }

          case term_function_type:
            return self->target_fixes()->function_type(*self, cast<FunctionTypeTerm>(term));

          case term_function_parameter:
          case term_function_type_parameter:
            return NULL;

          default:
            /**
             * Only terms which can be the type of a term should
             * appear here. This restricts us to term_functional,
             * term_apply, term_function_type and
             * term_function_parameter.
             *
             * term_recursive should only occur inside term_apply.
             *
             * term_recursive_parameter should never be encountered
             * since it should be expanded out by ApplyTerm::apply().
             */
            PSI_FAIL("unexpected type term type");
          }
        }

        boost::optional<const llvm::Type*> invalid() const {return boost::none;}
        bool valid(const boost::optional<const llvm::Type*>& t) const {return t;}
      };

      ConstantBuilder::ConstantBuilder(Context *context,
				       llvm::LLVMContext *llvm_context,
				       llvm::TargetMachine *target_machine,
				       TargetFixes *target_fixes)
        : m_context(context),
	  m_llvm_context(llvm_context),
	  m_llvm_target_machine(target_machine),
	  m_target_fixes(target_fixes),
	  m_empty_value(0) {
        PSI_ASSERT(m_llvm_context);
      }

      ConstantBuilder::~ConstantBuilder() {
      }

      /**
       * \brief Return the type specified by the specified term.
       *
       * Note that this is not the LLVM type of the LLVM value of this
       * term: it is the LLVM type of the LLVM value of terms whose type
       * is this term.
       */
      const llvm::Type* ConstantBuilder::build_type(Term* term) {
        boost::optional<const llvm::Type*> result = build_term(m_type_terms, term, TypeBuilderCallback(this)).first;
        return result ? *result : NULL;
      }

      /**
       * Get the size of a type.
       */
      uint64_t ConstantBuilder::type_size(const llvm::Type *ty) {
        return llvm_target_machine()->getTargetData()->getTypeAllocSize(ty);
      }

      /**
       * Get the alignment of a type
       */
      unsigned ConstantBuilder::type_alignment(const llvm::Type *ty) {
        return llvm_target_machine()->getTargetData()->getABITypeAlignment(ty);
      }

      /**
       * Get the size of a type from a type term.
       */
      uint64_t ConstantBuilder::constant_type_size(Term *type) {
	llvm::Constant *size_align = build_constant_simple(type);
	return metatype_constant_size(size_align).getZExtValue();
      }

      /**
       * Get the alignment of a type from a type term.
       */
      unsigned ConstantBuilder::constant_type_alignment(Term *type) {
	llvm::Constant *size_align = build_constant_simple(type);
	return metatype_constant_align(size_align).getZExtValue();
      }

      /**
       * Get the type used to represent intptr_t.
       */
      const llvm::IntegerType* ConstantBuilder::intptr_type() {
        return llvm_target_machine()->getTargetData()->getIntPtrType(llvm_context());
      }

      /**
       * Get the number of bits in an intptr_t.
       */
      unsigned ConstantBuilder::intptr_type_bits() {
        return intptr_type()->getBitWidth();
      }

      /**
       * \brief Return the constant value specified by a given term,
       * assuming that it can be exactly represented as an LLVM
       * constant.
       *
       * \pre <tt>!term->phantom() && term->global()</tt>
       */
      llvm::Constant* ConstantBuilder::build_constant_simple(Term *term) {
        ConstantValue *value = build_constant(term);
	PSI_ASSERT(value->simple_type());
	return value->simple_value();
      }

      /**
       * \brief Return the constant integer specified by the given term.
       *
       * This assumes that the conversion can be performed; this is
       * asserted by debug checks.
       *
       * \pre <tt>!term->phantom() && term->global()</tt>
       */
      const llvm::APInt& ConstantBuilder::build_constant_integer(Term *term) {
        llvm::Constant *c = build_constant_simple(term);
        PSI_ASSERT(llvm::isa<llvm::ConstantInt>(c));
        return llvm::cast<llvm::ConstantInt>(c)->getValue();
      }

      /**
       * Utility function to get an LLVM float type from a float
       * width, bypassing the normal build_type mechanism.
       */
      const llvm::Type* ConstantBuilder::get_float_type(FloatType::Width width) {
	switch (width) {
	case FloatType::fp32:  return llvm::Type::getFloatTy(llvm_context());
	case FloatType::fp64:  return llvm::Type::getDoubleTy(llvm_context());
	case FloatType::fp128: return llvm::Type::getFP128Ty(llvm_context());
	case FloatType::fp_x86_80:   return llvm::Type::getX86_FP80Ty(llvm_context());
	case FloatType::fp_ppc_128:  return llvm::Type::getPPC_FP128Ty(llvm_context());
	default: PSI_FAIL("unknown floating point width");
	}
      }

      /**
       * Utility function to get an LLVM integer type from a float
       * width, bypassing the normal build_type mechanism.
       */
      const llvm::IntegerType* ConstantBuilder::get_integer_type(IntegerType::Width width) {
	switch (width) {
	case IntegerType::i8:   return llvm::IntegerType::get(llvm_context(), 8);
	case IntegerType::i16:  return llvm::IntegerType::get(llvm_context(), 16);
	case IntegerType::i32:  return llvm::IntegerType::get(llvm_context(), 32);
	case IntegerType::i64:  return llvm::IntegerType::get(llvm_context(), 64);
	case IntegerType::i128: return llvm::IntegerType::get(llvm_context(), 128);
	case IntegerType::iptr: return intptr_type();
	default: PSI_FAIL("unknown integer width");
	}
      }

      struct GlobalBuilder::ConstantBuilderCallback : PtrValidBase<ConstantValue> {
        GlobalBuilder *self;
        ConstantBuilderCallback(GlobalBuilder *self_) : self(self_) {}

        ConstantValue* build(Term *term) const {
          switch (term->term_type()) {
          case term_functional:
            return self->build_constant_internal(cast<FunctionalTerm>(term));
           
          case term_apply: {
            Term* actual = cast<ApplyTerm>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            return self->build_constant(actual);
          }

          case term_global_variable:
          case term_function: {
            llvm::GlobalValue *value = self->build_global(cast<GlobalTerm>(term));
            llvm::Constant *i8ptr_value = llvm::ConstantExpr::getPointerCast(value, llvm::Type::getInt8PtrTy(self->llvm_context()));
            return self->new_constant_value_simple(term->type(), i8ptr_value);
          }

          default:
            PSI_FAIL("unexpected type term type");
          }
        }
      };

      struct GlobalBuilder::GlobalBuilderCallback : PtrValidBase<llvm::GlobalValue> {
        GlobalBuilder *self;
        GlobalBuilderCallback(GlobalBuilder *self_) : self(self_) {}

        llvm::GlobalValue* build(GlobalTerm *term) const {
          switch (term->term_type()) {
          case term_global_variable: {
            GlobalVariableTerm *global = cast<GlobalVariableTerm>(term);
            Term *global_type = cast<PointerType>(global->type())->target_type();
            const llvm::Type *llvm_type;
	    unsigned align = 1;
	    if (global->value()) {
	      GlobalResult<const llvm::Type> global_result = self->build_global_type(global->value());
	      align = global_result.alignment;
	      llvm_type = global_result.value;

	      uint64_t padding = self->constant_type_size(global->value_type()) - self->type_size(global_result.value);
	      if (padding > 0) {
		const llvm::Type *padding_type = llvm::ArrayType::get(llvm::Type::getInt8Ty(self->llvm_context()), padding);
		std::vector<const llvm::Type*> elements;
		elements.push_back(global_result.value);
		elements.push_back(padding_type);
		llvm_type = llvm::StructType::get(self->llvm_context(), elements, false);
	      }
            } else if ((llvm_type = self->build_type(global->value_type()))) {
	    } else {
              align = self->constant_type_alignment(global_type);
	      llvm_type = llvm::ArrayType::get(llvm::Type::getInt8Ty(self->llvm_context()), self->constant_type_size(global_type));
            }
	    llvm::GlobalValue *result = new llvm::GlobalVariable(self->llvm_module(), llvm_type,
								 global->constant(), llvm::GlobalValue::InternalLinkage,
								 NULL, global->name());
	    result->setAlignment(align);
	    return result;
          }

          case term_function: {
            FunctionTerm *func = cast<FunctionTerm>(term);
            PointerType::Ptr type = cast<PointerType>(func->type());
            FunctionTypeTerm* func_type = cast<FunctionTypeTerm>(type->target_type());
            const llvm::Type *llvm_type = self->build_type(func_type);
            PSI_ASSERT_MSG(llvm_type, "could not create function because its LLVM type is not known");
            return llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_type),
                                          llvm::GlobalValue::InternalLinkage,
                                          func->name(), &self->llvm_module());
          }

          default:
            PSI_FAIL("unexpected global term type");
          }
        }
      };

      GlobalBuilder::GlobalBuilder(Context *context, llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, TargetFixes *target_fixes, llvm::Module *module)
        : ConstantBuilder(context, llvm_context, target_machine, target_fixes), m_module(module) {
	llvm::Constant *empty_value = llvm::ConstantStruct::get(*llvm_context, 0, 0, false);
	m_empty_value = new_constant_value_simple(EmptyType::get(*context), empty_value);
      }

      GlobalBuilder::~GlobalBuilder() {
      }

      /**
       * Set the module created globals will be put into.
       */
      void GlobalBuilder::set_module(llvm::Module *module) {
        m_module = module;
      }

      /**
       * \brief Get the global variable specified by the given term.
       */
      llvm::GlobalValue* GlobalBuilder::build_global(GlobalTerm* term) {
        PSI_ASSERT((term->term_type() == term_function) || (term->term_type() == term_global_variable));

        std::pair<llvm::GlobalValue*, bool> gv = build_term(m_global_terms, term, GlobalBuilderCallback(this));

        if (gv.second) {
          if (m_global_build_list.empty()) {
            m_global_build_list.push_back(std::make_pair(term, gv.first));
            while (!m_global_build_list.empty()) {
              const std::pair<Term*, llvm::GlobalValue*>& t = m_global_build_list.front();
              if (t.first->term_type() == term_function) {
                IRBuilder irbuilder(llvm_context(), llvm::TargetFolder(llvm_target_machine()->getTargetData()));
                FunctionBuilder fb(this,
                                   cast<FunctionTerm>(t.first),
                                   llvm::cast<llvm::Function>(t.second),
                                   &irbuilder);
                fb.run();
              } else {
                PSI_ASSERT(t.first->term_type() == term_global_variable);
		GlobalVariableTerm *global_variable = cast<GlobalVariableTerm>(t.first);
                if (Term* init_value = global_variable->value()) {
		  GlobalResult<llvm::Constant> value = build_global_value(init_value);
		  llvm::Constant *final_value = value.value;

		  uint64_t padding = constant_type_size(global_variable->value_type()) - type_size(value.value->getType());
		  if (padding > 0) {
		    const llvm::Type *padding_type = llvm::ArrayType::get(llvm::Type::getInt8Ty(llvm_context()), padding);
		    llvm::Constant *padding_value = llvm::UndefValue::get(padding_type);
		    std::vector<llvm::Constant*> elements;
		    elements.push_back(value.value);
		    elements.push_back(padding_value);
		    final_value = llvm::ConstantStruct::get(llvm_context(), elements, false);
		  }
                  llvm::cast<llvm::GlobalVariable>(t.second)->setInitializer(final_value);
		}
              }
              m_global_build_list.pop_front();
            }
          } else {
            m_global_build_list.push_back(std::make_pair(term, gv.first));
          }
        }

        return gv.first;
      }

      ConstantValue* GlobalBuilder::build_constant(Term *term) {
        PSI_ASSERT(!term->phantom() && term->global());

        switch (term->term_type()) {
        case term_function:
        case term_global_variable:
        case term_apply:
        case term_functional:
          return build_term(m_constant_terms, term, ConstantBuilderCallback(this)).first;

        default:
          PSI_FAIL("constant builder encountered unexpected term type");
        }
      }

      LLVMJit::LLVMJit(Context *context,
		       const std::string& host_triple,
		       llvm::TargetMachine *host_machine)
        : m_context(context),
          m_target_fixes(create_target_fixes(host_triple)),
          m_builder(context, &m_llvm_context, host_machine, m_target_fixes.get()),
          m_llvm_engine(make_engine(m_llvm_context)) {
      }

      LLVMJit::~LLVMJit() {
      }

      void* LLVMJit::get_global(GlobalTerm *global) {
        std::auto_ptr<llvm::Module> module(new llvm::Module("", m_llvm_context));
        m_builder.set_module(module.get());
        llvm::GlobalValue *llvm_global = m_builder.build_global(global);
        m_builder.set_module(0);
        m_llvm_engine->addModule(module.release());
        return m_llvm_engine->getPointerToGlobal(llvm_global);
      }

      /**
       * Register a listener for the JIT. Just a passthrough to the
       * corresponding method on llvm::ExecutionEngine.
       */
      void LLVMJit::register_llvm_jit_listener(llvm::JITEventListener *l) {
        m_llvm_engine->RegisterJITEventListener(l);
      }

      /**
       * Unregister a listener for the JIT. Just a passthrough to the
       * corresponding method on llvm::ExecutionEngine.
       */
      void LLVMJit::unregister_llvm_jit_listener(llvm::JITEventListener *l) {
        m_llvm_engine->UnregisterJITEventListener(l);
      }

      /**
       * Create the LLVM Jit.
       */
      llvm::ExecutionEngine* LLVMJit::make_engine(llvm::LLVMContext& context) {
        llvm::Module *module = new llvm::Module(".tvm_fake_jit", context);
        llvm::EngineBuilder builder(module);
        llvm::ExecutionEngine *engine = builder.create();
        PSI_ASSERT_MSG(engine, "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
        return engine;
      }
    }

    /**
     * Create a JIT compiler using LLVM as a backend.
     */
    boost::shared_ptr<Jit> create_llvm_jit(Context *context) {
      llvm::InitializeNativeTarget();

      std::string host = llvm::sys::getHostTriple();

      std::string error_msg;
      const llvm::Target *target = llvm::TargetRegistry::lookupTarget(host, error_msg);
      if (!target)
	throw LLVM::BuildError("Could not get LLVM JIT target: " + error_msg);

      llvm::TargetMachine *tm = target->createTargetMachine(host, "");
      if (!tm)
	throw LLVM::BuildError("Failed to create target machine");

      return boost::make_shared<LLVM::LLVMJit>(context, host, tm);
    }
  }
}
