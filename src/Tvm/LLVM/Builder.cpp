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

      BuildError::BuildError(const std::string& msg)
        : std::logic_error(msg) {
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

          case term_function_type: {
            FunctionTypeTerm* actual = cast<FunctionTypeTerm>(term);
            if (actual->calling_convention() == cconv_tvm) {
              const llvm::Type* i8ptr = llvm::Type::getInt8PtrTy(self->llvm_context());
              std::vector<const llvm::Type*> params(actual->n_parameters() - actual->n_phantom_parameters() + 1, i8ptr);
              return llvm::FunctionType::get(llvm::Type::getVoidTy(self->llvm_context()), params, false);
            } else {
              std::size_t n_phantom = actual->n_phantom_parameters();
              std::size_t n_parameters = actual->n_parameters() - n_phantom;
              std::vector<const llvm::Type*> params;
              for (std::size_t i = 0; i < n_parameters; ++i) {
                const llvm::Type *param_type = self->build_type(actual->parameter(i+n_phantom)->type());
                if (!param_type)
                  return NULL;
                params.push_back(param_type);
              }
              const llvm::Type *result_type = self->build_type(actual->result_type());
              if (!result_type)
                return NULL;
              return llvm::FunctionType::get(result_type, params, false);
            }
          }

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

      ConstantBuilder::ConstantBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine)
        : m_llvm_context(context), m_llvm_target_machine(target_machine) {
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
      uint64_t ConstantBuilder::type_alignment(const llvm::Type *ty) {
        return llvm_target_machine()->getTargetData()->getABITypeAlignment(ty);
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
       * \brief Return the constant integer specified by the given term.
       *
       * This is currently just a utility function which runs
       * build_constant and converts the result to an integer, raising
       * an exception if this is not possible.
       */
      const llvm::APInt& ConstantBuilder::build_constant_integer(Term *term) {
        llvm::ConstantInt *ci = llvm::dyn_cast<llvm::ConstantInt>(build_constant(term));
        if (!ci)
          throw BuildError("cannot convert constant term to an integer");

        return ci->getValue();
      }

      struct GlobalBuilder::ConstantBuilderCallback : PtrValidBase<llvm::Constant> {
        GlobalBuilder *self;
        ConstantBuilderCallback(GlobalBuilder *self_) : self(self_) {}

        llvm::Constant* build(Term *term) const {
          switch (term->term_type()) {
          case term_functional:
            return self->build_constant_internal(cast<FunctionalTerm>(term));
           
          case term_apply: {
            Term* actual = cast<ApplyTerm>(term)->unpack();
            PSI_ASSERT(actual->term_type() != term_apply);
            return self->build_constant(actual);
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
            const llvm::Type *llvm_type = self->build_type(global_type);
            if (llvm_type) {
              return new llvm::GlobalVariable(self->llvm_module(), llvm_type,
                                              global->constant(), llvm::GlobalValue::InternalLinkage,
                                              NULL, global->name());
            } else {
              llvm::Constant *size_align = self->build_constant(global_type);
              llvm::APInt size = metatype_constant_size(size_align);
              llvm::APInt align = metatype_constant_align(size_align);
            }
          }

          case term_function: {
            FunctionTerm *func = cast<FunctionTerm>(term);
            PointerType::Ptr type = cast<PointerType>(func->type());
            FunctionTypeTerm* func_type = cast<FunctionTypeTerm>(type->target_type());
            const llvm::Type *llvm_type = self->build_type(func_type);
            if (!llvm_type)
              throw BuildError("could not create function because its LLVM type is not known");
            return llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_type),
                                          llvm::GlobalValue::InternalLinkage,
                                          func->name(), &self->llvm_module());
          }

          default:
            PSI_FAIL("unexpected global term type");
          }
        }
      };

      GlobalBuilder::GlobalBuilder(llvm::LLVMContext *context, llvm::TargetMachine *target_machine, llvm::Module *module)
        : ConstantBuilder(context, target_machine), m_module(module) {
        PSI_ASSERT(m_module);
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
                if (Term* init_value = cast<GlobalVariableTerm>(t.first)->value())
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

      llvm::Constant* GlobalBuilder::build_constant(Term *term) {
        PSI_ASSERT(!term->phantom() && term->global());

        switch (term->term_type()) {
        case term_function:
        case term_global_variable:
          return build_global(cast<GlobalTerm>(term));

        case term_apply:
        case term_functional:
          return build_term(m_constant_terms, term, ConstantBuilderCallback(this)).first;

        default:
          throw BuildError("constant builder encountered unexpected term type");
        }
      }

      /**
       * Get the host machine data.
       */
      llvm::TargetMachine* host_machine() {
        std::string host = llvm::sys::getHostTriple();

        std::string error_msg;
        const llvm::Target *target = llvm::TargetRegistry::lookupTarget(host, error_msg);
        if (!target)
          throw BuildError("Could not get LLVM JIT target: " + error_msg);

        llvm::TargetMachine *tm = target->createTargetMachine(host, "");
        if (!tm)
          throw BuildError("Failed to create target machine");

        return tm;
      }

      LLVMJit::LLVMJit(Context *context)
        : m_context(context),
          m_builder(&m_llvm_context, host_machine()),
          m_llvm_engine(make_engine(m_llvm_context)) {

        llvm::InitializeNativeTarget();
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
      return boost::make_shared<LLVM::LLVMJit>(context);
    }
  }
}
