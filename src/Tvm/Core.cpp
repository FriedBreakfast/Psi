#include "Core.hpp"
#include "Type.hpp"

#include <llvm/Type.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/IRBuilder.h>
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
    ContextObject::ContextObject(const UserInitializer& ui, Context *context)
      : User(ui), m_context(context) {
      m_context->m_gc_objects.push_back(this);
    }

    ContextObject::~ContextObject() {
    }

    Context::Context() {
      init();
    }

    Context::~Context() {
      while (!m_gc_objects.empty())
	delete m_gc_objects.pop_front();
    }

    std::pair<void*, UserInitializer> Context::allocate_user(std::size_t obj_size,
							     std::size_t n_uses) {
      std::size_t s = (obj_size + sizeof(Use) - 1) & ~AlignOf<Use>::value;
      void *p = operator new (s + (sizeof(Use)*(n_uses+1)));
      Use *pu = static_cast<Use*>(static_cast<void*>(static_cast<char*>(p)+s));
      return std::make_pair(p, UserInitializer(n_uses, pu));
    }

    void Context::init() {
      init_types();
    }

#if 0
    void Context::init_llvm() {
      llvm::InitializeNativeTarget();

      std::string host = llvm::sys::getHostTriple();

      std::string error_msg;
      const llvm::Target *target = llvm::TargetRegistry::lookupTarget(host, error_msg);
      if (!target)
	throw std::runtime_error("Could not get LLVM JIT target: " + error_msg);

      m_llvm_target_machine = target->createTargetMachine(host, "");
      if (!m_llvm_target_machine)
	throw std::runtime_error("Failed to create target machine");

      m_llvm_target_data = m_llvm_target_machine->getTargetData();
    }
#endif

    void Context::init_types() {
      m_metatype = Metatype::create(this);
      m_type_empty = EmptyType::create(this);
      m_type_label = LabelType::create(this);
      m_type_pointer = PointerType::create(this);

      m_type_void = NULL;
      m_type_int8 = IntegerType::create(this, 8, true);
      m_type_uint8 = IntegerType::create(this, 8, false);
      m_type_int16 = IntegerType::create(this, 16, true);
      m_type_uint16 = IntegerType::create(this, 16, false);
      m_type_int32 = IntegerType::create(this, 32, true);
      m_type_uint32 = IntegerType::create(this, 32, false);
      m_type_int64 = IntegerType::create(this, 64, true);
      m_type_uint64 = IntegerType::create(this, 64, false);
      m_type_real32 = NULL;
      m_type_real64 = NULL;
      m_type_real128 = NULL;
    }

    void* Context::term_jit(Term *term) {
      LLVMBuilderValue value = m_builder.value(term);
      PSI_ASSERT((value.category() == LLVMBuilderValue::global) && llvm::isa<llvm::GlobalValue>(value.value()),
		 "Cannot JIT compile a value which is not global");
      const llvm::GlobalValue *global = llvm::cast<llvm::GlobalValue>(value.value());

      if (!m_llvm_engine) {
	llvm::InitializeNativeTarget();
	m_llvm_engine.reset(llvm::EngineBuilder(m_builder.m_module.release()).create());
	PSI_ASSERT(m_llvm_engine.get(), "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
      } else {
	m_llvm_engine->addModule(m_builder.m_module.release());
      }

      m_builder.m_module.reset(new llvm::Module("", *m_builder.m_context));

      return m_llvm_engine->getPointerToGlobal(global);
    }

    Term::Term(const UserInitializer& ui,
	       Context *context,
	       TermType *type,
	       bool constant,
	       bool global)
      : ContextObject(ui, context),
	m_constant(constant), m_global(global) {
      use_set(slot_type, type);
    }

    TermType::TermType(const UserInitializer& ui, Context *context, bool constant, bool global)
      : Term(ui, context, context->metatype(), constant, global) {
    }

    TermType::TermType(const UserInitializer& ui, Context *context, Metatype*)
      : Term(ui, context, NULL, true, true) {
    }

    struct Metatype::Initializer : InitializerBase<Metatype, Metatype::slot_max> {
      Metatype* operator() (void *p, const UserInitializer& ui, Context *context) const {
	return new (p) Metatype (ui, context);
      }
    };

    Metatype* Metatype::create(Context *context) {
      return context->new_user(Initializer());
    }

    Metatype::Metatype(const UserInitializer& ui, Context *context)
      : TermType(ui, context, this) {
    }

    LLVMBuilderValue Metatype::build_llvm_value(LLVMBuilder&) {
      throw std::logic_error("Metatype does not have a value");
    }

    LLVMBuilderType Metatype::build_llvm_type(LLVMBuilder& builder) {
      llvm::LLVMContext& context = builder.context();
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context);
      return LLVMBuilderType::known_type(llvm::StructType::get(context, i64, i64, NULL));
    }

    LLVMBuilderValue Metatype::llvm_value(const llvm::Type* ty) {
      llvm::Constant* values[2] = {
	llvm::ConstantExpr::getSizeOf(ty),
	llvm::ConstantExpr::getAlignOf(ty)
      };

      return LLVMBuilderValue::global_value(llvm::ConstantStruct::get(ty->getContext(), values, 2, false));
    }

    LLVMBuilderValue Metatype::llvm_value_empty(llvm::LLVMContext& context) {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(context);
      llvm::Constant* values[2] = {
	llvm::ConstantInt::get(i64, 0),
	llvm::ConstantInt::get(i64, 1)
      };

      return LLVMBuilderValue::global_value(llvm::ConstantStruct::get(context, values, 2, false));
    }

    LLVMBuilderValue Metatype::llvm_value_global(llvm::Constant *size, llvm::Constant *align) {
      llvm::LLVMContext& context = size->getContext();
      PSI_ASSERT(size->getType()->isIntegerTy(64) && align->getType()->isIntegerTy(64),
		 "size and align members of Metatype must both be i64");
      PSI_ASSERT(!llvm::cast<llvm::ConstantInt>(align)->equalsInt(0), "align cannot be zero");
      llvm::Constant* values[2] = {size, align};
      return LLVMBuilderValue::global_value(llvm::ConstantStruct::get(context, values, 2, false));
    }

    LLVMBuilderValue Metatype::llvm_value_local(LLVMBuilder& builder, llvm::Value *size, llvm::Value *align) {
      LLVMBuilder::IRBuilder& irbuilder = builder.irbuilder();
      llvm::LLVMContext& context = builder.context();
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context);
      llvm::Type *mtype = llvm::StructType::get(context, i64, i64, NULL);
      llvm::Value *first = irbuilder.CreateInsertValue(llvm::UndefValue::get(mtype), size, 0);
      llvm::Value *second = irbuilder.CreateInsertValue(first, align, 1);
      return LLVMBuilderValue::known_value(second);
    }

    Type::Type(const UserInitializer& ui, Context *context, bool constant, bool global)
      : TermType(ui, context, constant, global) {
    }

    Value::Value(const UserInitializer& ui, Context *context, Type *type, bool constant, bool global)
      : Term(ui, context, type, constant, global) {
      PSI_ASSERT(type, "Type of a Value cannot be null");
    }

    LLVMBuilderType Value::build_llvm_type(LLVMBuilder&) {
      throw std::logic_error("build_llvm_type should never be called on value instances");
    }
  }
}
