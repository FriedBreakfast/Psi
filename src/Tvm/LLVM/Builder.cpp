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
#include <llvm/Support/Host.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>

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

      struct ModuleBuilder::TypeBuilderCallback : PtrValidBase<llvm::Type> {
        ModuleBuilder *self;
        TypeBuilderCallback(ModuleBuilder *self_) : self(self_) {}

        llvm::Type* build(Term* term) const {
          switch(term->term_type()) {
          case term_functional:
            return self->build_type_internal(cast<FunctionalTerm>(term));

          case term_apply: {
            Term* actual = cast<ApplyTerm>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            return self->build_type(actual);
          }

          case term_function_type: {
            llvm::SmallVector<llvm::Type*, 8> params;
            FunctionTypeTerm *function_type = cast<FunctionTypeTerm>(term);
            for (std::size_t i = function_type->n_phantom_parameters(), e = function_type->n_parameters(); i != e; ++i)
              params.push_back(self->build_type(function_type->parameter_type(i)));
            llvm::Type *result = self->build_type(function_type->result_type());
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

      /**
       * \brief Return the constant integer specified by the given term.
       *
       * This assumes that the conversion can be performed; this is
       * asserted by debug checks.
       *
       * \pre <tt>!term->phantom() && term->global()</tt>
       */
      const llvm::APInt& ModuleBuilder::build_constant_integer(Term *term) {
        llvm::Constant *c = build_constant(term);
        PSI_ASSERT(llvm::isa<llvm::ConstantInt>(c));
        return llvm::cast<llvm::ConstantInt>(c)->getValue();
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
      
      namespace {
        /// \brief Utility function used by intrinsic_memcpy_64 and
        /// intrinsic_memcpy_32.
        llvm::Function* intrinsic_memcpy(llvm::Module& m, llvm::TargetMachine *target_machine) {
          llvm::IntegerType *size_type = target_machine->getTargetData()->getIntPtrType(m.getContext());
          const char *name;
          switch (size_type->getBitWidth()) {
          case 32: name = "llvm.memcpy.p0i8.p0i8.i32"; break;
          case 64: name = "llvm.memcpy.p0i8.p0i8.i64"; break;
          default: PSI_FAIL("unsupported bit width for memcpy parameter");
          }
          
          llvm::Function *f = m.getFunction(name);
          if (f)
            return f;

          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {
            llvm::Type::getInt8PtrTy(c),
            llvm::Type::getInt8PtrTy(c),
            size_type,
            llvm::Type::getInt32Ty(c),
            llvm::Type::getInt1Ty(c)
          };
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
          f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

          return f;
        }

        llvm::Function* intrinsic_stacksave(llvm::Module& m) {
          const char *name = "llvm.stacksave";
          llvm::Function *f = m.getFunction(name);
          if (f)
            return f;

          llvm::LLVMContext& c = m.getContext();
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(c), llvm::ArrayRef<llvm::Type*>(), false);
          f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

          return f;
        }

        llvm::Function* intrinsic_stackrestore(llvm::Module& m) {
          const char *name = "llvm.stackrestore";
          llvm::Function *f = m.getFunction(name);
          if (f)
            return f;

          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {llvm::Type::getInt8PtrTy(c)};
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
          f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

          return f;
        }
        
        llvm::Function* intrinsic_eh_exception(llvm::Module& m) {
          const char *name = "llvm.eh.exception";
          if (llvm::Function *f = m.getFunction(name))
            return f;
          
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(m.getContext()), false);
          return llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);
        }
        
        llvm::Function* intrinsic_eh_selector(llvm::Module& m) {
          const char *name = "llvm.eh.selector";
          if (llvm::Function *f = m.getFunction(name))
            return f;

          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {llvm::Type::getInt8PtrTy(c), llvm::Type::getInt8PtrTy(c)};
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(m.getContext()), args, true);
          return llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);
        }
        
        llvm::Function* intrinsic_eh_typeid_for(llvm::Module& m) {
          const char *name = "llvm.eh.typeid.for";
          if (llvm::Function *f = m.getFunction(name))
            return f;
          
          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {llvm::Type::getInt8PtrTy(c)};
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(m.getContext()), args, false);
          return llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);
        }
      }

      ModuleBuilder::ModuleBuilder(llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, llvm::Module *llvm_module, TargetCallback *target_callback)
        : m_llvm_context(llvm_context), m_llvm_target_machine(target_machine), m_llvm_module(llvm_module), m_target_callback(target_callback) {
        m_llvm_memcpy = intrinsic_memcpy(*llvm_module, target_machine);
        m_llvm_stacksave = intrinsic_stacksave(*llvm_module);
        m_llvm_stackrestore = intrinsic_stackrestore(*llvm_module);
        m_llvm_eh_exception = intrinsic_eh_exception(*llvm_module);
        m_llvm_eh_selector = intrinsic_eh_selector(*llvm_module);
        m_llvm_eh_typeid_for = intrinsic_eh_typeid_for(*llvm_module);
      }

      ModuleBuilder::~ModuleBuilder() {
      }

      ModuleMapping ModuleBuilder::run(Module *module) {
        ModuleMapping module_result;
        module_result.module = m_llvm_module;
        
        AggregateLoweringPass aggregate_lowering_pass(module, target_callback()->aggregate_lowering_callback());
        aggregate_lowering_pass.remove_all_unions = true;
        aggregate_lowering_pass.remove_only_unknown = true;
        aggregate_lowering_pass.remove_stack_arrays = true;
        aggregate_lowering_pass.remove_sizeof = true;
        aggregate_lowering_pass.update();
        
        for (Module::ModuleMemberList::iterator i = module->members().begin(), e = module->members().end(); i != e; ++i) {
          GlobalTerm *term = &*i;
          GlobalTerm *rewritten_term = aggregate_lowering_pass.target_symbol(term);
          llvm::GlobalValue *result;
          switch (rewritten_term->term_type()) {
          case term_global_variable: {
            GlobalVariableTerm *global = cast<GlobalVariableTerm>(rewritten_term);
            llvm::Type *llvm_type = build_type(global->value_type());
            result = new llvm::GlobalVariable(*m_llvm_module, llvm_type,
                                              global->constant(), llvm::GlobalValue::ExternalLinkage,
                                              NULL, global->name());
            break;
          }

          case term_function: {
            FunctionTerm *func = cast<FunctionTerm>(rewritten_term);
            PointerType::Ptr type = cast<PointerType>(func->type());
            FunctionTypeTerm* func_type = cast<FunctionTypeTerm>(type->target_type());
            llvm::Type *llvm_type = build_type(func_type);
            PSI_ASSERT_MSG(llvm_type, "could not create function because its LLVM type is not known");
            result = llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_type),
                                            llvm::GlobalValue::ExternalLinkage,
                                            func->name(), m_llvm_module);
            break;
          }

          default:
            PSI_FAIL("unexpected global term type");
          }

          if (term->alignment())
            result->setAlignment(build_constant_integer(term->alignment()).getZExtValue());
          
          m_global_terms[rewritten_term] = result;
          module_result.globals[term] = result;
        }
        
        for (Module::ModuleMemberList::iterator i = module->members().begin(), e = module->members().end(); i != e; ++i) {
          GlobalTerm *term = &*i;
          GlobalTerm *rewritten_term = aggregate_lowering_pass.target_symbol(term);
          PSI_ASSERT(m_global_terms.find(rewritten_term) != m_global_terms.end());
          llvm::GlobalValue *llvm_term = m_global_terms.find(rewritten_term)->second;
          
          if (rewritten_term->term_type() == term_function) {
            FunctionBuilder fb(this, cast<FunctionTerm>(rewritten_term), llvm::cast<llvm::Function>(llvm_term));
            fb.run();
          } else {
            PSI_ASSERT(term->term_type() == term_global_variable);
            if (Term *value = cast<GlobalVariableTerm>(rewritten_term)->value()) {
              llvm::Constant *llvm_value = build_constant(value);
              llvm::cast<llvm::GlobalVariable>(llvm_term)->setInitializer(llvm_value);
            } else {
              llvm::GlobalVariable *gv = llvm::cast<llvm::GlobalVariable>(llvm_term);
              gv->setInitializer(llvm::UndefValue::get(llvm::cast<llvm::PointerType>(gv->getType())->getElementType()));
            }
          }
        }
        
        return module_result;
      }
      
      /**
       * \brief Get the global variable specified by the given term.
       */
      llvm::GlobalValue* ModuleBuilder::build_global(GlobalTerm* term) {
        std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*>::iterator it = m_global_terms.find(term);
        if (it == m_global_terms.end())
          throw BuildError("Cannot find global term");
        return it->second;
      }

      /**
        * \brief Return the constant value specified by the given term.
        *
        * \pre <tt>!term->phantom() && term->global()</tt>
        */
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

      /**
       * \brief Return the type specified by the specified term.
       *
       * Note that this is not the LLVM type of the LLVM value of this
       * term: it is the LLVM type of the LLVM value of terms whose type
       * is this term.
       */
      llvm::Type* ModuleBuilder::build_type(Term* term) {
        return build_term(m_type_terms, term, TypeBuilderCallback(this)).first;
      }
      
      llvm::IntegerType* integer_type(llvm::LLVMContext& context, const llvm::TargetData *target_data, IntegerType::Width width) {
        unsigned bits;
        switch (width) {
        case IntegerType::i8: bits = 8; break;
        case IntegerType::i16: bits = 16; break;
        case IntegerType::i32: bits = 32; break;
        case IntegerType::i64: bits = 64; break;
        case IntegerType::i128: bits = 128; break;
        case IntegerType::iptr: return target_data->getIntPtrType(context);
        default: PSI_FAIL("unknown integer width");
        }
        return llvm::Type::getIntNTy(context, bits);
      }
      
      llvm::Type* float_type(llvm::LLVMContext& context, FloatType::Width width) {
        switch (width) {
        case FloatType::fp32: return llvm::Type::getFloatTy(context);
        case FloatType::fp64: return llvm::Type::getDoubleTy(context);
        case FloatType::fp128: return llvm::Type::getFP128Ty(context);
        case FloatType::fp_x86_80: return llvm::Type::getX86_FP80Ty(context);
        case FloatType::fp_ppc_128: return llvm::Type::getPPC_FP128Ty(context);
        default: PSI_FAIL("unknown float width"); break;
        }
      }

      LLVMJit::LLVMJit(const boost::shared_ptr<JitFactory>& jit_factory,
                       const std::string& host_triple,
		       const boost::shared_ptr<llvm::TargetMachine>& host_machine)
        : Jit(jit_factory),
          m_target_fixes(create_target_fixes(&m_llvm_context, host_machine, host_triple)),
          m_target_machine(host_machine) {
      }

      LLVMJit::~LLVMJit() {
      }

      void LLVMJit::add_module(Module *module) {
        std::auto_ptr<llvm::Module> llvm_module(new llvm::Module(module->name(), m_llvm_context));
        ModuleBuilder builder(&m_llvm_context, m_target_machine.get(), llvm_module.get(), m_target_fixes.get());
        ModuleMapping new_mapping = builder.run(module);

        ModuleMapping& mapping = m_modules[module];
        if (mapping.module)
          throw BuildError("module already exists in this JIT");
        mapping = new_mapping;

        if (!m_llvm_engine) {
          init_llvm_engine(llvm_module.get());
        } else {
          m_llvm_engine->addModule(llvm_module.get());
        }

        llvm_module.release();
      }
      
      void LLVMJit::remove_module(Module *module) {
        std::tr1::unordered_map<Module*, ModuleMapping>::iterator it = m_modules.find(module);
        if (it == m_modules.end())
          throw BuildError("module not present");
        m_llvm_engine->removeModule(it->second.module);
        delete it->second.module;
        m_modules.erase(it);
      }
      
      void LLVMJit::rebuild_module(Module *module, bool) {
        remove_module(module);
        add_module(module);
      }
      
      void* LLVMJit::get_symbol(GlobalTerm *global) {
        Module *module = global->module();
        std::tr1::unordered_map<Module*, ModuleMapping>::iterator it = m_modules.find(module);
        if (it == m_modules.end())
          throw BuildError("Module does not appear to be available in this JIT");
        
        std::tr1::unordered_map<GlobalTerm*, llvm::GlobalValue*>::iterator jt = it->second.globals.find(global);
        PSI_ASSERT(jt != it->second.globals.end());
        
        return m_llvm_engine->getPointerToGlobal(jt->second);
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
      void LLVMJit::init_llvm_engine(llvm::Module *module) {
        m_llvm_engine.reset(llvm::ExecutionEngine::create(module, false, 0, llvm::CodeGenOpt::Default, false));
        PSI_ASSERT_MSG(m_llvm_engine, "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
        
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
    throw Psi::Tvm::LLVM::BuildError("Could not get LLVM target: " + error_msg);

  boost::shared_ptr<llvm::TargetMachine> tm(target->createTargetMachine(host, "", ""));
  if (!tm)
    throw Psi::Tvm::LLVM::BuildError("Failed to create target machine");
  
  return boost::make_shared<Psi::Tvm::LLVM::LLVMJit>(factory, host, tm);
}
