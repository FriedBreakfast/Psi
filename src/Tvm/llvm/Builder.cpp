#include "Builder.hpp"
#include "Engine.hpp"

#include "../Aggregate.hpp"
#include "../Core.hpp"
#include "../Function.hpp"
#include "../Functional.hpp"
#include "../Recursive.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * Namespace containing classes to convert Tvm code to the LLVM
     * IR. The generated code will be machine-specific.
     */
    namespace LLVM {
#if PSI_DEBUG
      /**
       * For some reason GDB doesn't pick up llvm::Value::dump a lot.
       */
      void dump_llvm_value(void *p) {
        static_cast<llvm::Value*>(p)->dump();
      }
#endif

      /**
       * \brief Return the constant integer specified by the given term.
       *
       * This assumes that the conversion can be performed; this is
       * asserted by debug checks.
       *
       * \pre <tt>!term->phantom() && term->global()</tt>
       */
      const llvm::APInt& ModuleBuilder::build_constant_integer(const ValuePtr<>& term) {
        llvm::Constant *c = build_constant(term);
        PSI_ASSERT(llvm::isa<llvm::ConstantInt>(c));
        return llvm::cast<llvm::ConstantInt>(c)->getValue();
      }

      namespace {
        /// \brief Utility function used by intrinsic_memcpy_64 and
        /// intrinsic_memcpy_32.
        llvm::Function* intrinsic_memcpy(llvm::Module& m, llvm::TargetMachine *target_machine) {
          llvm::IntegerType *size_type = target_machine->getDataLayout()->getIntPtrType(m.getContext());
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

        /// \brief Utility function used by intrinsic_memcpy_64 and
        /// intrinsic_memcpy_32.
        llvm::Function* intrinsic_memset(llvm::Module& m, llvm::TargetMachine *target_machine) {
          llvm::IntegerType *size_type = target_machine->getDataLayout()->getIntPtrType(m.getContext());
          const char *name;
          switch (size_type->getBitWidth()) {
          case 32: name = "llvm.memset.p0i8.i32"; break;
          case 64: name = "llvm.memset.p0i8.i64"; break;
          default: PSI_FAIL("unsupported bit width for memcpy parameter");
          }
          
          llvm::Function *f = m.getFunction(name);
          if (f)
            return f;

          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {
            llvm::Type::getInt8PtrTy(c),
            llvm::Type::getInt8Ty(c),
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
        
        llvm::Function* intrinsic_invariant_start(llvm::Module& m) {
          const char *name = "llvm.invariant.start";
          llvm::Function *f = m.getFunction(name);
          if (f)
            return f;
          
          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {llvm::Type::getInt64Ty(c), llvm::Type::getInt8PtrTy(c)};
          llvm::FunctionType *ft = llvm::FunctionType::get(llvm::StructType::get(c)->getPointerTo(), args, false);
          f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, name, &m);

          return f;
        }
        
        llvm::Function* intrinsic_invariant_end(llvm::Module& m) {
          const char *name = "llvm.invariant.end";
          llvm::Function *f = m.getFunction(name);
          if (f)
            return f;
          
          llvm::LLVMContext& c = m.getContext();
          llvm::Type *args[] = {llvm::StructType::get(c)->getPointerTo(), llvm::Type::getInt64Ty(c), llvm::Type::getInt8PtrTy(c)};
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

      ModuleBuilder::ModuleBuilder(CompileErrorContext *error_context,
                                   llvm::LLVMContext *llvm_context, llvm::TargetMachine *target_machine, llvm::Module *llvm_module,
                                   TargetCallback *target_callback)
        : m_error_context(error_context), m_llvm_context(llvm_context),
        m_llvm_triple(target_machine->getTargetTriple()), m_llvm_target_machine(target_machine),
        m_llvm_module(llvm_module), m_target_callback(target_callback) {
        m_llvm_memcpy = intrinsic_memcpy(*llvm_module, target_machine);
        m_llvm_memset = intrinsic_memset(*llvm_module, target_machine);
        m_llvm_stacksave = intrinsic_stacksave(*llvm_module);
        m_llvm_stackrestore = intrinsic_stackrestore(*llvm_module);
        m_llvm_invariant_start = intrinsic_invariant_start(*llvm_module);
        m_llvm_invariant_end = intrinsic_invariant_end(*llvm_module);
        m_llvm_eh_exception = intrinsic_eh_exception(*llvm_module);
        m_llvm_eh_selector = intrinsic_eh_selector(*llvm_module);
        m_llvm_eh_typeid_for = intrinsic_eh_typeid_for(*llvm_module);
      }

      ModuleBuilder::~ModuleBuilder() {
      }
      
      /**
       * Whether the dllimport/dllexport flags should be used or not.
       * 
       * They are used on Windows, however must be disabled for modules
       * targetting the MCJIT.
       */
      bool ModuleBuilder::use_dllimport() {
        const llvm::Triple& t = llvm_triple();
        return t.isOSWindows() && (t.getEnvironment() != llvm::Triple::ELF);
      }
      
      /**
       * Get the LLVM equivalent of the specified linkage mode.
       */
      llvm::GlobalValue::LinkageTypes ModuleBuilder::llvm_linkage_for(Linkage linkage) {
        switch (linkage) {
        case link_local: return llvm::GlobalValue::LinkerPrivateLinkage;
        case link_private: return llvm::GlobalValue::ExternalLinkage;
        case link_one_definition: return llvm::GlobalValue::LinkOnceODRLinkage;
        case link_export: return use_dllimport() ? llvm::GlobalValue::DLLExportLinkage : llvm::GlobalValue::ExternalLinkage;
        case link_import: return use_dllimport() ? llvm::GlobalValue::DLLImportLinkage : llvm::GlobalValue::ExternalLinkage;
        default: PSI_FAIL("Unknown linkage type");
        }
      }
      
      /**
       * Apply any additional modifications due to the specified linkage type.
       */
      void ModuleBuilder::apply_linkage(Linkage linkage, llvm::GlobalValue *value) {
        llvm::GlobalValue::VisibilityTypes visibility;
        switch (linkage) {
        case link_local: visibility = llvm::GlobalValue::HiddenVisibility; break;
        case link_private: visibility = llvm::GlobalValue::HiddenVisibility; break;
        case link_one_definition: visibility = llvm::GlobalValue::HiddenVisibility; break;
        case link_export: visibility = llvm::GlobalValue::ProtectedVisibility; break;
        case link_import: visibility = llvm::GlobalValue::DefaultVisibility; break;
        default: PSI_FAIL("Unknown linkage type");
        }
        value->setVisibility(visibility);
      }
      
      ModuleMapping ModuleBuilder::run(Module *module) {
        ModuleMapping module_result;
        
        AggregateLoweringPass aggregate_lowering_pass(module, target_callback()->aggregate_lowering_callback());
        aggregate_lowering_pass.remove_unions = true;
        aggregate_lowering_pass.memcpy_to_bytes = true;
        aggregate_lowering_pass.update();
        
        Module *rewritten_module = aggregate_lowering_pass.target_module();
        
        for (Module::ModuleMemberList::iterator i = module->members().begin(), e = module->members().end(); i != e; ++i) {
          const ValuePtr<Global>& old_term = i->second;
          ValuePtr<Global> term = aggregate_lowering_pass.target_symbol(old_term);
          
          llvm::GlobalValue *result;
          llvm::GlobalValue::LinkageTypes linkage = llvm_linkage_for(term->linkage());
          switch (term->term_type()) {
          case term_global_variable: {
            ValuePtr<GlobalVariable> global = value_cast<GlobalVariable>(term);
            llvm::Type *llvm_type = build_type(global->value_type());
            result = new llvm::GlobalVariable(*m_llvm_module, llvm_type,
                                              global->constant(), linkage,
                                              NULL, global->name());
            if (global->constant() && global->merge())
              result->setUnnamedAddr(true);
            break;
          }

          case term_function: {
            ValuePtr<Function> func = value_cast<Function>(term);
            ValuePtr<PointerType> type = value_cast<PointerType>(func->type());
            ValuePtr<FunctionType> func_type = value_cast<FunctionType>(type->target_type());
            llvm::Type *llvm_type = build_type(func_type);
            PSI_ASSERT_MSG(llvm_type, "could not create function because its LLVM type is not known");
            llvm::Function *llvm_func = llvm::Function::Create(llvm::cast<llvm::FunctionType>(llvm_type),
                                                               linkage, func->name(), m_llvm_module);
            llvm_func->setAttributes(function_type_attributes(llvm_context(), func_type));
            llvm_func->setCallingConv(function_call_convention(error_context().bind(func->location()), func_type->calling_convention()));
            result = llvm_func;
            break;
          }

          default:
            PSI_FAIL("unexpected global term type");
          }

          apply_linkage(term->linkage(), result);
          if (term->alignment())
            result->setAlignment(build_constant_integer(term->alignment()).getZExtValue());
          
          m_global_terms[term] = result;
          module_result[old_term] = result;
        }
        
        for (Module::ModuleMemberList::iterator i = rewritten_module->members().begin(), e = rewritten_module->members().end(); i != e; ++i) {
          const ValuePtr<Global>& term = i->second;
          PSI_ASSERT(m_global_terms.find(term) != m_global_terms.end());
          llvm::GlobalValue *llvm_term = m_global_terms.find(term)->second;
          
          if (term->term_type() == term_function) {
            ValuePtr<Function> func = value_cast<Function>(term);
            if (!func->blocks().empty()) {
              llvm::Function *llvm_func = llvm::cast<llvm::Function>(llvm_term);
              FunctionBuilder fb(this, func, llvm_func);
              fb.run();
            }
          } else {
            PSI_ASSERT(term->term_type() == term_global_variable);
            if (ValuePtr<> value = value_cast<GlobalVariable>(term)->value()) {
              llvm::Constant *llvm_value = build_constant(value);
              llvm::cast<llvm::GlobalVariable>(llvm_term)->setInitializer(llvm_value);
            }
          }
        }
        
        build_constructor_list("llvm.global_ctors", aggregate_lowering_pass.target_module()->constructors());
        build_constructor_list("llvm.global_dtors", aggregate_lowering_pass.target_module()->destructors());
        
        return module_result;
      }
      
      /**
       * \brief Get the global variable specified by the given term.
       */
      llvm::GlobalValue* ModuleBuilder::build_global(const ValuePtr<Global>& term) {
        boost::unordered_map<ValuePtr<Global>, llvm::GlobalValue*>::iterator it = m_global_terms.find(term);
        if (it == m_global_terms.end())
          error_context().error_throw(term->location(), "Cannot find global term");
        return it->second;
      }

      /**
        * \brief Return the constant value specified by the given term.
        *
        * \pre <tt>!term->phantom() && term->global()</tt>
        */
      llvm::Constant* ModuleBuilder::build_constant(const ValuePtr<>& term) {
        std::pair<ConstantTermMap::iterator, bool> itp =
          m_constant_terms.insert(std::make_pair(term, static_cast<llvm::Constant*>(NULL)));
        if (!itp.second) {
          if (!itp.first->second)
            error_context().error_throw(term->location(), "Cyclical term found");
          return itp.first->second;
        }

        llvm::Constant *r;
        switch (term->term_type()) {
        case term_functional:
          r = build_constant_internal(value_cast<FunctionalValue>(term));
          break;

        case term_global_variable:
        case term_function:
          r = build_global(value_cast<Global>(term));
          break;
          
        case term_apply: {
          ValuePtr<> actual = value_cast<ApplyType>(term)->unpack();
          PSI_ASSERT(actual->term_type() != term_apply);
          r = build_constant(actual);
          break;
        }

        default:
          PSI_FAIL("constant builder encountered unexpected term type");
        }
        
        if (!r) {
          m_constant_terms.erase(itp.first);
          error_context().error_throw(term->location(), "LLVM term building failed");
        }
        
        itp.first->second = r;
        return r;
      }

      /**
       * \brief Return the type specified by the specified term.
       *
       * Note that this is not the LLVM type of the LLVM value of this
       * term: it is the LLVM type of the LLVM value of terms whose type
       * is this term.
       */
      llvm::Type* ModuleBuilder::build_type(const ValuePtr<>& term) {
        std::pair<TypeTermMap::iterator, bool> itp =
          m_type_terms.insert(std::make_pair(term, static_cast<llvm::Type*>(NULL)));
        if (!itp.second) {
          if (!itp.first->second)
            error_context().error_throw(term->location(), "Cyclical term found");
          return itp.first->second;
        }

        llvm::Type *t;
        switch(term->term_type()) {
        case term_functional:
          t = build_type_internal(value_cast<FunctionalValue>(term));
          break;

        case term_apply: {
          ValuePtr<> actual = value_cast<ApplyType>(term)->unpack();
          PSI_ASSERT(actual->term_type() != term_apply);
          t = build_type(actual);
          break;
        }

        case term_function_type: {
          llvm::SmallVector<llvm::Type*, 8> params;
          ValuePtr<FunctionType> function_type = value_cast<FunctionType>(term);
          PSI_ASSERT(function_type->n_phantom() == 0);
          int sret = function_type->sret() ? 1 : 0;
          if (sret)
            params.push_back(build_type(function_type->parameter_types().back().value));
          for (std::size_t i = 0, e = function_type->parameter_types().size() - sret; i != e; ++i)
            params.push_back(build_type(function_type->parameter_types()[i].value));
          llvm::Type *result = isa<EmptyType>(function_type->result_type().value) ?
            llvm::Type::getVoidTy(*m_llvm_context) : build_type(function_type->result_type().value);
          t = llvm::FunctionType::get(result, params, false);
          break;
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
        
        if (!t) {
          m_type_terms.erase(itp.first);
          error_context().error_throw(term->location(), "LLVM type building failed");
        }
        
        itp.first->second = t;
        return t;
      }
      
      /**
       * \brief Build a constructor or destructor list.
       */
      void ModuleBuilder::build_constructor_list(const char *name, const Module::ConstructorList& constructors) {
        if (constructors.empty())
          return;
        
        std::vector<llvm::Constant*> elements;
        llvm::Type *priority_type = llvm::Type::getInt32Ty(llvm_context());
        llvm::Type *constructor_ptr_type = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_context()), false)->getPointerTo();
        llvm::StructType *element_type = llvm::StructType::get(priority_type, constructor_ptr_type, NULL);
        for (Module::ConstructorList::const_iterator ii = constructors.begin(), ie = constructors.end(); ii != ie; ++ii) {
          llvm::GlobalValue *function = build_global(ii->first);
          llvm::Constant *priority = llvm::ConstantInt::get(priority_type, ii->second);
          llvm::Constant *values[2] = {priority, function};
          elements.push_back(llvm::ConstantStruct::getAnon(values));
        }
        llvm::ArrayType *constructor_list_type = llvm::ArrayType::get(element_type, elements.size());
        llvm::Constant *constructor_list = llvm::ConstantArray::get(constructor_list_type, elements);
        
        new llvm::GlobalVariable(*m_llvm_module, constructor_list_type,
                                 true, llvm::GlobalValue::AppendingLinkage, constructor_list, name);
      }
      
      llvm::IntegerType* integer_type(llvm::LLVMContext& context, const llvm::DataLayout *target_data, IntegerType::Width width) {
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
    }
  }
}
