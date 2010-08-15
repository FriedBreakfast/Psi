#include "Core.hpp"
#include "Type.hpp"

#include <llvm/LLVMContext.h>
#include <llvm/Type.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Module.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
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
      const llvm::Value *value = m_builder.value(term);
      PSI_ASSERT(llvm::isa<llvm::GlobalValue>(value), "Cannot JIT compile a value which is not global");
      const llvm::GlobalValue *global = llvm::cast<llvm::GlobalValue>(value);

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
	       bool constant)
      : ContextObject(ui, context), m_constant(constant) {
      use_set(slot_type, type);
    }

    TermType::TermType(const UserInitializer& ui, Context *context, bool constant)
      : Term(ui, context, context->metatype(), constant) {
    }

    TermType::TermType(const UserInitializer& ui, Context *context, Metatype*)
      : Term(ui, context, NULL, true) {
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

    const llvm::Value* Metatype::build_llvm_value(LLVMBuilder&) {
      return NULL;
    }

    const llvm::Type* Metatype::build_llvm_type(LLVMBuilder& builder) {
      llvm::LLVMContext& context = builder.context();
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context);
      return llvm::StructType::get(context, i64, i64, NULL);
    }

    const llvm::Value* Metatype::llvm_value(llvm::LLVMContext& context, const llvm::Type* ty) {
      llvm::Constant* values[2] = {
	llvm::ConstantExpr::getSizeOf(ty),
	llvm::ConstantExpr::getAlignOf(ty)
      };

      return llvm::ConstantStruct::get(context, values, 2, false);
    }

    Type::Type(const UserInitializer& ui, Context *context, bool constant)
      : TermType(ui, context, constant) {
    }

    Value::Value(const UserInitializer& ui, Context *context, Type *type, bool constant)
      : Term(ui, context, type, constant) {
      PSI_ASSERT(type, "Type of a Value cannot be null");
    }

    const llvm::Type* Value::build_llvm_type(LLVMBuilder&) {
      throw std::logic_error("build_llvm_type should never be called on value instances");
    }

    LLVMBuilder::LLVMBuilder()
      : m_context(new llvm::LLVMContext),
	m_module(new llvm::Module("", *m_context)) {
    }

    LLVMBuilder::~LLVMBuilder() {
    }

    const llvm::Value* LLVMBuilder::value(Term *term) {
      ValueMap::iterator it = m_value_map.find(term);
      if (it != m_value_map.end())
	return it->second;

      const llvm::Value *value = term->build_llvm_value(*this);
      m_value_map.insert(ValueMap::value_type(term, value));
      return value;
    }

    const llvm::Type* LLVMBuilder::type(Term *term) {
      TypeMap::iterator it = m_type_map.find(term);
      if (it != m_type_map.end())
	return it->second;

      const llvm::Type *type = term->build_llvm_type(*this);
      m_type_map.insert(TypeMap::value_type(term, type));
      return type;
    }
  }
}
