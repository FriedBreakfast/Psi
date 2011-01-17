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
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/System/Host.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetRegistry.h>
#include <llvm/Target/TargetSelect.h>

#ifdef PSI_DEBUG
#include <iostream>
#include <llvm/CodeGen/MachineFunction.h>
#endif

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

      struct ConstantBuilder::TypeBuilderCallback : PtrValidBase<llvm::Type> {
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

          case term_function_type: {
            std::vector<const llvm::Type*> params;
            FunctionTypeTerm *function_type = cast<FunctionTypeTerm>(term);
            for (std::size_t i = function_type->n_phantom_parameters(), e = function_type->n_parameters(); i != e; ++i)
              params.push_back(self->build_type(function_type->parameter_type(i)));
            const llvm::Type *result = self->build_type(function_type->result_type());
            return llvm::FunctionType::get(result, params, false);
          }

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
      };

      ConstantBuilder::ConstantBuilder(llvm::LLVMContext *llvm_context,
				       llvm::TargetMachine *target_machine)
        : m_llvm_context(llvm_context),
	  m_llvm_target_machine(target_machine) {
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
        return build_term(m_type_terms, term, TypeBuilderCallback(this)).first;
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
	return build_constant_integer(MetatypeSize::get(type)).getZExtValue();
      }

      /**
       * Get the alignment of a type from a type term.
       */
      unsigned ConstantBuilder::constant_type_alignment(Term *type) {
	return build_constant_integer(MetatypeAlignment::get(type)).getZExtValue();
      }

      /**
       * Get the type used to represent intptr_t.
       */
      const llvm::IntegerType* ConstantBuilder::get_intptr_type() {
        return llvm_target_machine()->getTargetData()->getIntPtrType(llvm_context());
      }

      /**
       * Get the number of bits in an intptr_t.
       */
      unsigned ConstantBuilder::intptr_type_bits() {
        return get_intptr_type()->getBitWidth();
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
        llvm::Constant *c = build_constant(term);
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
       * Utility function to get the LLVM type used to represent a
       * boolean value.
       */
      const llvm::IntegerType* ConstantBuilder::get_boolean_type() {
	return llvm::Type::getInt1Ty(llvm_context());
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
	case IntegerType::iptr: return get_intptr_type();
	default: PSI_FAIL("unknown integer width");
	}
      }

      /**
       * Get the type used to represent one byte, i.e. the units of
       * size for this system.
       */
      const llvm::Type* ConstantBuilder::get_byte_type() {
	return llvm::Type::getInt8Ty(llvm_context());
      }

      /**
       * Utility function to return the LLVM type used to represent
       * all pointers. This will always be a pointer to the type
       * returned by get_byte_type.
       */
      const llvm::Type* ConstantBuilder::get_pointer_type() {
	return get_byte_type()->getPointerTo();
      }

      struct ModuleBuilder::ConstantBuilderCallback : PtrValidBase<llvm::Constant> {
        ModuleBuilder *self;
        ConstantBuilderCallback(ModuleBuilder *self_) : self(self_) {}

        llvm::Constant* build(Term *term) const {
          switch (term->term_type()) {
          case term_functional:
            return self->build_constant_internal(cast<FunctionalTerm>(term));
           
          case term_apply: {
            Term* actual = cast<ApplyTerm>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            return self->build_constant(actual);
          }

          case term_global_variable:
          case term_function:
            return self->build_global(cast<GlobalTerm>(term));

          default:
            PSI_FAIL("unexpected type term type");
          }
        }
      };

      struct ModuleBuilder::GlobalBuilderCallback : PtrValidBase<llvm::GlobalValue> {
        ModuleBuilder *self;
        GlobalBuilderCallback(ModuleBuilder *self_) : self(self_) {}

        llvm::GlobalValue* build(GlobalTerm *term) const {
          llvm::GlobalValue *result;
          switch (term->term_type()) {
          case term_global_variable: {
            GlobalVariableTerm *global = cast<GlobalVariableTerm>(term);
            const llvm::Type *llvm_type = self->build_type(global->value_type());
	    result = new llvm::GlobalVariable(self->llvm_module(), llvm_type,
                                              global->constant(), llvm::GlobalValue::ExternalLinkage,
                                              NULL, global->name());
            break;
          }

          case term_function: {
            FunctionTerm *func = cast<FunctionTerm>(term);
            PointerType::Ptr type = cast<PointerType>(func->type());
            FunctionTypeTerm* func_type = cast<FunctionTypeTerm>(type->target_type());
            const llvm::Type *llvm_type = self->build_type(func_type);
            PSI_ASSERT_MSG(llvm_type, "could not create function because its LLVM type is not known");
            result = llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_type),
                                            llvm::GlobalValue::ExternalLinkage,
                                            func->name(), &self->llvm_module());
          }

          default:
            PSI_FAIL("unexpected global term type");
          }
          
          result->setAlignment(term->alignment());
          return result;
        }
      };

      ModuleBuilder::ModuleBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, llvm::Module *module)
        : ConstantBuilder(llvm_context, target_machine), m_module(module) {
      }

      ModuleBuilder::~ModuleBuilder() {
      }

      /**
       * Set the module created globals will be put into.
       */
      void ModuleBuilder::set_module(llvm::Module *module) {
        m_module = module;
      }

      /**
       * \brief Get the global variable specified by the given term.
       */
      llvm::GlobalValue* ModuleBuilder::build_global(GlobalTerm* term) {
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
                if (Term* init_value = global_variable->value())
                  llvm::cast<llvm::GlobalVariable>(t.second)->setInitializer(build_constant(init_value));
              }
              m_global_build_list.pop_front();
            }
          } else {
            m_global_build_list.push_back(std::make_pair(term, gv.first));
          }
        }

        return gv.first;
      }

      llvm::Constant* ModuleBuilder::build_constant(Term *term) {
        PSI_ASSERT(!term->phantom() && (!term->source() || isa<GlobalTerm>(term->source())));

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
      
      LLVMJit::LLVMJit(const boost::shared_ptr<JitFactory>& jit_factory,
                       const std::string& host_triple,
		       llvm::TargetMachine *host_machine)
        : Jit(jit_factory),
          m_target_fixes(create_target_fixes(host_triple)),
          m_builder(&m_llvm_context, host_machine) {
      }

      LLVMJit::~LLVMJit() {
      }

      void LLVMJit::add_module(Module *module) {
      }
      
      void LLVMJit::remove_module(Module *module) {
      }
      
      void LLVMJit::rebuild_module(Module *module, bool incremental) {
      }
      
      void* LLVMJit::get_symbol(GlobalTerm *global) {
        std::auto_ptr<llvm::Module> module(new llvm::Module("", m_llvm_context));
        m_builder.set_module(module.get());
        llvm::GlobalValue *llvm_global = m_builder.build_global(global);
        m_builder.set_module(0);
        m_llvm_engine->addModule(module.release());
        return m_llvm_engine->getPointerToGlobal(llvm_global);
      }

#ifdef PSI_DEBUG
      class DebugListener : public llvm::JITEventListener {
      public:
        DebugListener(bool dump_ir, bool dump_asm)
          : m_dump_ir(dump_ir), m_dump_asm(dump_asm) {
        }

        virtual void NotifyFunctionEmitted (const llvm::Function &F, void*, size_t, const EmittedFunctionDetails& details) {
          llvm::raw_os_ostream out(std::cerr);
          if (m_dump_ir)
            F.print(out);
          if (m_dump_asm)
            details.MF->print(out);
        }

        //virtual void NotifyFreeingMachineCode (void *OldPtr)

      private:
        bool m_dump_ir;
        bool m_dump_asm;
      };
#endif

      /**
       * Create the LLVM Jit.
       */
      llvm::ExecutionEngine* LLVMJit::make_engine(llvm::Module *module) {
        llvm::EngineBuilder builder(module);
        llvm::ExecutionEngine *engine = builder.create();
        PSI_ASSERT_MSG(engine, "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
        
#ifdef PSI_DEBUG
        const char *debug_mode = std::getenv("PSI_LLVM_DEBUG");
        if (debug_mode) {
          bool dump_ir, dump_asm;
          if (std::strcmp(debug_mode, "all") == 0) {
            dump_ir = dump_asm = true;
          } else if (std::strcmp(debug_mode, "asm") == 0) {
            dump_ir = false; dump_asm = true;
          } else if (std::strcmp(debug_mode, "ir") == 0) {
            dump_ir = true; dump_asm = false;
          } else {
            dump_ir = dump_asm = false;
          }
          
          if (dump_asm || dump_ir) {
            m_debug_listener = boost::make_shared<DebugListener>(dump_ir, dump_asm);
            m_llvm_engine->RegisterJITEventListener(m_debug_listener.get());
          }
        }
#endif
        
        return engine;
      }
    }
  }
}

extern "C" boost::shared_ptr<Psi::Tvm::Jit> tvm_jit_new(const boost::shared_ptr<Psi::Tvm::JitFactory>& factory) {
  llvm::InitializeNativeTarget();
  std::string host = llvm::sys::getHostTriple();

  std::string error_msg;
  const llvm::Target *target = llvm::TargetRegistry::lookupTarget(host, error_msg);
  if (!target)
    throw Psi::Tvm::LLVM::BuildError("Could not get LLVM JIT target: " + error_msg);

  llvm::TargetMachine *tm = target->createTargetMachine(host, "");
  if (!tm)
    throw Psi::Tvm::LLVM::BuildError("Failed to create target machine");
  
  return boost::make_shared<Psi::Tvm::LLVM::LLVMJit>(factory, host, tm);
}
