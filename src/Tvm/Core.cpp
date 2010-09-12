#include "Core.hpp"
//#include "Derived.hpp"
//#include "Function.hpp"

#include <stdexcept>
#include <typeinfo>

#include <boost/checked_delete.hpp>

#include <llvm/LLVMContext.h>
#include <llvm/Type.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/GlobalVariable.h>
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
    namespace {
      inline std::size_t struct_offset(std::size_t base, std::size_t size, std::size_t align) {
	return (base + size + align - 1) & ~align;
      }

      inline void* ptr_offset(void *p, std::size_t offset) {
	return static_cast<void*>(static_cast<char*>(p) + offset);
      }

      template<typename T>
      struct InitializerBase {
	typedef T* ResultType;
	static const std::size_t size = sizeof(T);
      };
    }

    template<typename T>
    typename T::ResultType Context::allocate_term(const T& initializer) {
      std::size_t use_offset = struct_offset(0, initializer.size, align_of<Use>());
      std::size_t total_size = use_offset + sizeof(Use)*(initializer.n_slots+2);

      void *term_base = operator new (total_size);
      Use *uses = static_cast<Use*>(ptr_offset(term_base, use_offset));
      try {
	return initializer.init(term_base, UserInitializer(initializer.n_slots+1, uses), this);
      } catch(...) {
	operator delete (term_base);
	throw;
      }
    }

    template<typename T>
    typename T::ResultType Context::allocate_distinct_term(const T& initializer) {
      typename T::ResultType rt = allocate_term(initializer);
      m_distinct_terms.push_back(*rt);
      return rt;
    }

    struct MetatypeTerm::Initializer : InitializerBase<MetatypeTerm> {
      static const std::size_t n_slots = 0;
      MetatypeTerm* init(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) MetatypeTerm(ui, context);
      }
    };

    Context::Context()
      : m_functional_terms_n_buckets(functional_terms_n_initial_buckets),
	m_functional_terms_buckets(new FunctionalTermSet::bucket_type[functional_terms_n_initial_buckets]),
	m_functional_terms(FunctionalTermSet::bucket_traits(m_functional_terms_buckets.get(), m_functional_terms_n_buckets)) {
      m_metatype.reset(allocate_term(MetatypeTerm::Initializer()));
    }

    struct Context::DistinctTermDisposer {
      void operator () (DistinctTerm *t) const {
	switch(t->m_term_type) {
	case Term::term_function: delete static_cast<FunctionTerm*>(t); break;
	case Term::term_global_variable: delete static_cast<GlobalVariableTerm*>(t); break;
	case Term::term_opaque: delete static_cast<OpaqueTerm*>(t); break;
	default: PSI_FAIL("cannot dispose of unknown type");
	}
      }
    };

    struct Context::FunctionalBaseTermDisposer {
      void operator () (FunctionalBaseTerm *t) const {
	switch (t->m_term_type) {
	case Term::term_functional: delete static_cast<FunctionalTerm*>(t); break;
	case Term::term_function_type: delete static_cast<FunctionTypeTerm*>(t); break;
	case Term::term_function_type_parameter: delete static_cast<FunctionTypeParameterTerm*>(t); break;
	case Term::term_opaque_resolver: delete static_cast<OpaqueResolverTerm*>(t); break;
	default: PSI_FAIL("cannot dispose of unknown type");
	}
      }
    };

    Context::~Context() {
      m_functional_terms.clear_and_dispose(FunctionalBaseTermDisposer());
      m_distinct_terms.clear_and_dispose(DistinctTermDisposer());
    }

    struct OpaqueTerm::Initializer : InitializerBase<OpaqueTerm> {
      static const std::size_t n_slots = 0;
      Term *type;
      Initializer(Term *type_) : type(type_) {}
      OpaqueTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) OpaqueTerm(ui, context, type);
      }
    };

    OpaqueTerm* Context::new_opaque(Term *type) {
      return allocate_distinct_term(OpaqueTerm::Initializer(type));
    }

    struct GlobalVariableTerm::Initializer : InitializerBase<GlobalVariableTerm> {
      static const std::size_t n_slots = 0;
      Term *type;
      bool constant;
      Initializer(Term *type_, bool constant_) : type(type_), constant(constant_) {}
      GlobalVariableTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) GlobalVariableTerm(ui, context, type, constant);
      }
    };

    GlobalVariableTerm* Context::new_global_variable(Term *type, bool constant) {
      return allocate_distinct_term(GlobalVariableTerm::Initializer(type, constant));
    }

    std::size_t Context::FunctionalBaseTermHash::operator () (const FunctionalBaseTerm& t) const {
      return t.m_hash;
    }

    std::size_t Context::term_hash(const Term *t) {
      switch (t->m_term_type) {
      case Term::term_functional:
      case Term::term_function_type:
      case Term::term_function_type_parameter:
      case Term::term_opaque_resolver:
	return static_cast<const FunctionalBaseTerm*>(t)->m_hash;

      default:
	return boost::hash_value(t);
      }
    }

    void Context::check_functional_terms_rehash() {
      if (m_functional_terms.size() >= m_functional_terms.bucket_count()) {
	std::size_t new_n_buckets = m_functional_terms_n_buckets*2;
	UniqueArray<FunctionalTermSet::bucket_type> new_buckets(new FunctionalTermSet::bucket_type[new_n_buckets]);
	m_functional_terms.rehash(FunctionalTermSet::bucket_traits(new_buckets.get(), new_n_buckets));

	m_functional_terms_n_buckets = new_n_buckets;
	m_functional_terms_buckets.swap(new_buckets);
      }
    }

    namespace {
      struct HashKey {
	std::size_t hash;
      };

      struct HashKeyHash {
	std::size_t operator () (const HashKey& k) const {
	  return k.hash;
	}
      };

      struct FunctionalTermKey : HashKey {
	const FunctionalTermBackend *backend;
	std::size_t n_parameters;
	Term *const* parameters;
      };
    }

    struct Context::FunctionalTermKeyEquals {
      bool operator () (const FunctionalTermKey& key, const FunctionalBaseTerm& value) const {
	if ((key.hash != value.m_hash) || (value.m_term_type != Term::term_functional) || (key.n_parameters != value.n_parameters()))
	  return false;

	const FunctionalTerm& cast_value = static_cast<const FunctionalTerm&>(value);
	for (std::size_t i = 0; i < key.n_parameters; ++i) {
	  if (key.parameters[i] != cast_value.parameter(i))
	    return false;
	}

	if (!key.backend->equals(*cast_value.m_backend))
	  return false;

	return true;
      };
    };

    struct FunctionalTerm::Initializer {
      typedef FunctionalTerm* ResultType;

      std::size_t n_slots;
      std::size_t proto_offset;
      std::size_t size;
      std::size_t hash;
      Term *type;
      const ValueCloner<FunctionalTermBackend> *backend_cloner;
      std::size_t n_parameters;
      Term *const* parameters;

      Initializer(std::size_t hash_, Term *type_, const ValueCloner<FunctionalTermBackend>* backend_cloner_, std::size_t n_parameters_, Term *const* parameters_)
	: n_slots(n_parameters), hash(hash_), type(type_), backend_cloner(backend_cloner_), n_parameters(n_parameters_), parameters(parameters_) {
	proto_offset = struct_offset(0, sizeof(FunctionalTerm), backend_cloner->align);
	size = proto_offset + backend_cloner->size;
      }

      FunctionalTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	FunctionalTermBackend *backend = backend_cloner->clone(ptr_offset(base, proto_offset));
	try {
	  return new (base) FunctionalTerm(ui, context, type, hash, backend, n_parameters, parameters);
	} catch(...) {
	  backend->~FunctionalTermBackend();
	  throw;
	}
      }
    };

    FunctionalTerm* Context::get_functional_internal(const FunctionalTermBackend& backend, const ValueCloner<FunctionalTermBackend>& backend_cloner,
						     std::size_t n_parameters, Term *const* parameters) {
      FunctionalTermKey key;
      key.backend = &backend;
      key.n_parameters = n_parameters;
      key.parameters = parameters;

      key.hash = 0;
      boost::hash_combine(key.hash, backend.hash_value());
      boost::hash_combine(key.hash, Term::term_functional);
      for (std::size_t i = 0; i < n_parameters; ++i)
	boost::hash_combine(key.hash, term_hash(parameters[i]));

      FunctionalTermSet::insert_commit_data commit_data;
      std::pair<FunctionalTermSet::iterator, bool> existing =
	m_functional_terms.insert_check(key, HashKeyHash(), FunctionalTermKeyEquals(), commit_data);
      if (!existing.second) {
	PSI_ASSERT(existing.first->m_term_type == Term::term_functional, "functional term type error");
	return static_cast<FunctionalTerm*>(&*existing.first);
      }

      Term *type = backend.type(*this, n_parameters, parameters);
      FunctionalTerm *term = allocate_term(FunctionalTerm::Initializer(key.hash, type, &backend_cloner, n_parameters, parameters));
      m_functional_terms.insert_commit(*term, commit_data);
      check_functional_terms_rehash();
      return term;
    }

    namespace {
      struct FunctionTypeTermKey : HashKey {
	std::size_t n_parameters;
	Term *const* parameter_types;
	Term *result_type;
      };
    }

    struct Context::FunctionTypeTermKeyEquals {
      bool operator () (const FunctionTypeTermKey& key, const FunctionalBaseTerm& value) const {
	if ((key.hash != value.m_hash) || (value.m_term_type != Term::term_function_type))
	  return false;

	const FunctionTypeTerm& cast_value = static_cast<const FunctionTypeTerm&>(value);
	if (key.n_parameters != cast_value.n_function_parameters())
	  return false;

	for (std::size_t i = 0; i < key.n_parameters; ++i) {
	  if (key.parameter_types[i] != cast_value.function_parameter(i))
	    return false;
	}

	if (key.result_type != cast_value.function_result_type())
	  return false;

	return true;
      }
    };

    struct FunctionTypeTerm::Initializer : InitializerBase<FunctionTypeTerm> {
      std::size_t n_slots;
      std::size_t hash;
      Term *result_type;
      std::size_t n_parameters;
      Term *const* parameter_types;
      Initializer(std::size_t hash_, Term *result_type_, std::size_t n_parameters_, Term *const* parameter_types_)
	: n_slots(n_parameters+1), hash(hash_), result_type(result_type_), n_parameters(n_parameters_), parameter_types(parameter_types_) {}
      FunctionTypeTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeTerm(ui, context, hash, result_type, n_parameters, parameter_types);
      }
    };

    FunctionTypeTerm* Context::get_function_type(Term *result_type, std::size_t n_parameters, Term *const* parameter_types) {
      FunctionTypeTermKey key;
      key.n_parameters = n_parameters;
      key.parameter_types = parameter_types;
      key.result_type = result_type;

      key.hash = 0;
      boost::hash_combine(key.hash, Term::term_function_type);
      boost::hash_combine(key.hash, term_hash(result_type));
      for (std::size_t i = 0; i < n_parameters; ++i)
	boost::hash_combine(key.hash, term_hash(parameter_types[i]));

      FunctionalTermSet::insert_commit_data commit_data;
      std::pair<FunctionalTermSet::iterator, bool> existing =
	m_functional_terms.insert_check(key, HashKeyHash(), FunctionTypeTermKeyEquals(), commit_data);
      if (!existing.second) {
	PSI_ASSERT(existing.first->m_term_type == Term::term_function_type, "functional term type error");
	return static_cast<FunctionTypeTerm*>(&*existing.first);
      }

      FunctionTypeTerm *term = allocate_term(FunctionTypeTerm::Initializer(key.hash, result_type, n_parameters, parameter_types));
      m_functional_terms.insert_commit(*term, commit_data);
      check_functional_terms_rehash();
      return term;
    }

    namespace {
      struct FunctionTypeParameterTermKey : HashKey {
	Term *type;
	Term *source;
	std::size_t index;
      };
    }

    struct Context::FunctionTypeParameterTermKeyEquals {
      bool operator () (const FunctionTypeParameterTermKey& key, const FunctionalBaseTerm& value) const {
	if (value.m_term_type != Term::term_function_type_parameter)
	  return false;

	const FunctionTypeParameterTerm& cast_value = static_cast<const FunctionTypeParameterTerm&>(value);
	if ((key.type != cast_value.type()) || (key.source != cast_value.source()))
	  return false;

	return true;
      }
    };

    struct FunctionTypeParameterTerm::Initializer : InitializerBase<FunctionTypeParameterTerm> {
      static const std::size_t n_slots = 1;
      Term *source, *type;
      std::size_t index, hash;
      Initializer(Term *type_, std::size_t hash_, Term *source_, std::size_t index_) : source(source_), type(type_), index(index_), hash(hash_) {}
      FunctionTypeParameterTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeParameterTerm(ui, context, type, hash, source, index);
      }
    };

    FunctionTypeParameterTerm* Context::get_function_type_parameter(Term *type, OpaqueTerm *func, std::size_t index) {
      FunctionTypeParameterTermKey key;
      key.type = type;
      key.source = func;
      key.index = index;

      key.hash = 0;
      boost::hash_combine(key.hash, Term::term_function_type_parameter);
      boost::hash_combine(key.hash, term_hash(type));
      boost::hash_combine(key.hash, term_hash(func));
      boost::hash_combine(key.hash, index);

      FunctionalTermSet::insert_commit_data commit_data;
      std::pair<FunctionalTermSet::iterator, bool> existing =
	m_functional_terms.insert_check(key, HashKeyHash(), FunctionTypeParameterTermKeyEquals(), commit_data);
      if (!existing.second) {
	PSI_ASSERT(existing.first->m_term_type == Term::term_function_type_parameter, "functional term type error");
	return static_cast<FunctionTypeParameterTerm*>(&*existing.first);
      }

      FunctionTypeParameterTerm *term = allocate_term(FunctionTypeParameterTerm::Initializer(type, key.hash, func, index));
      m_functional_terms.insert_commit(*term, commit_data);
      check_functional_terms_rehash();
      return term;
    }

    namespace {
      struct OpaqueResolverTermKey : HashKey {
	Term *type;
	std::size_t depth;
      };
    }

    struct Context::OpaqueResolverTermKeyEquals {
      bool operator () (const OpaqueResolverTermKey& key, const FunctionalBaseTerm& value) const {
	if (value.m_term_type != Term::term_opaque_resolver)
	  return false;

	const OpaqueResolverTerm& cast_value = static_cast<const OpaqueResolverTerm&>(value);
	if ((key.type != cast_value.type()) || (key.depth != cast_value.m_depth))
	  return false;

	return true;
      }
    };

    struct OpaqueResolverTerm::Initializer : InitializerBase<OpaqueResolverTerm> {
      static const std::size_t n_slots = 0;
      Term *type;
      std::size_t depth;
      std::size_t hash;
      Initializer(Term *type_, std::size_t depth_, std::size_t hash_) : type(type_), depth(depth_), hash(hash_) {}
      OpaqueResolverTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) OpaqueResolverTerm(ui, context, hash, type, depth);
      }
    };

    OpaqueResolverTerm* Context::get_opaque_resolver(std::size_t depth, Term *type) {
      OpaqueResolverTermKey key;
      key.type = type;
      key.depth = depth;

      key.hash = 0;
      boost::hash_combine(key.hash, Term::term_opaque_resolver);
      boost::hash_combine(key.hash, term_hash(type));
      boost::hash_combine(key.hash, depth);

      FunctionalTermSet::insert_commit_data commit_data;
      std::pair<FunctionalTermSet::iterator, bool> existing =
	m_functional_terms.insert_check(key, HashKeyHash(), OpaqueResolverTermKeyEquals(), commit_data);
      if (!existing.second) {
	PSI_ASSERT(existing.first->m_term_type == Term::term_function_type_parameter, "functional term type error");
	return static_cast<OpaqueResolverTerm*>(&*existing.first);
      }

      OpaqueResolverTerm *term = allocate_term(OpaqueResolverTerm::Initializer(type, depth, key.hash));
      m_functional_terms.insert_commit(*term, commit_data);
      check_functional_terms_rehash();
      return term;
    }

    Term* Context::build_resolver_term(std::size_t depth, OpaqueTerm *resolving, Term *term) {
      if (term->m_complete)
	return term;

      switch (term->m_term_type) {
      case Term::term_functional:

      case Term::term_function_type:

      case Term::term_function_type_parameter:

      case Term::term_opaque: {
	if (term == resolving)
	  return get_opaque_resolver(depth, resolving->type());
	else
	  return term;
      }

      default:
	PSI_FAIL("unexpected term type in opaque type resolution");
      }
    }

    Term* Context::resolve_opaque(OpaqueTerm *opaque, Term *term) {
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

    void* Context::term_jit(Term *term) {
      if ((term->m_term_type != Term::term_global_variable) &&
	  (term->m_term_type != Term::term_function))
	throw std::logic_error("Cannot JIT compile non-global term");

      if (!m_llvm_context) {
	m_llvm_context.reset(new llvm::LLVMContext());
	m_llvm_module.reset(new llvm::Module("", *m_llvm_context));
      }

#if 1
      PSI_FAIL("reimplement JIT compiling");
#else
      LLVMConstantBuilder builder(m_llvm_context.get(), m_llvm_module.get());
      llvm::GlobalValue *global = builder.global(term);

      if (!m_llvm_engine) {
	llvm::InitializeNativeTarget();
	m_llvm_engine.reset(llvm::EngineBuilder(m_llvm_module.release()).create());
	PSI_ASSERT(m_llvm_engine.get(), "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
      } else {
	m_llvm_engine->addModule(m_llvm_module.release());
      }

      m_llvm_module.reset(new llvm::Module("", *m_llvm_context));

      return m_llvm_engine->getPointerToGlobal(global);
#endif
    }

    Term::Term(const UserInitializer& ui, Context *context, TermType term_type, bool complete, Term *type)
      : User(ui), m_context(context), m_term_type(term_type), m_complete(complete) {

      if (!type) {
	m_category = category_metatype;
	PSI_ASSERT(complete, "metatype should always be complete");
      } else {
	PSI_ASSERT(context == type->m_context, "context mismatch between term and its type");
	if (type->m_category == category_metatype) {
	  m_category = category_type;
	} else {
	  PSI_ASSERT(type->m_category == category_type, "unknown category");
	  m_category = category_value;
	}
      }

      use_set(0, type);
    }

    std::size_t FunctionalTermBackend::hash_value() const {
      std::size_t value = hash_internal();

      const std::type_info& ti = typeid(*this);
#if __GXX_MERGED_TYPEINFO_NAMES
      boost::hash_combine(value, ti.name());
#else
      for (const char *p = ti.name(); *p != '\0'; ++p)
	boost::hash_combine(value, *p);
#endif
      return value;
    }

    MetatypeTerm::MetatypeTerm(const UserInitializer& ui, Context* context)
      : Term(ui, context, term_metatype, true, NULL) {
    }

    OpaqueTerm::OpaqueTerm(const UserInitializer& ui, Context *context, Term *type)
      : DistinctTerm(ui, context, term_opaque, false, type) {
    }

    Term* OpaqueTerm::resolve(Term *term) {
      return context().resolve_opaque(this, term);
    }

#if 0
    LLVMConstantBuilder::Type Metatype::llvm_type(LLVMConstantBuilder& builder, Term*) const {
      llvm::LLVMContext& context = builder.context();
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context);
      return LLVMConstantBuilder::type_known(llvm::StructType::get(context, i64, i64, NULL));
    }

    LLVMConstantBuilder::Constant Metatype::llvm_value(const llvm::Type* ty) {
      llvm::Constant* values[2] = {
	llvm::ConstantExpr::getSizeOf(ty),
	llvm::ConstantExpr::getAlignOf(ty)
      };

      return LLVMConstantBuilder::constant_value(llvm::ConstantStruct::get(ty->getContext(), values, 2, false));
    }

    LLVMConstantBuilder::Constant Metatype::llvm_value_empty(llvm::LLVMContext& context) {
      const llvm::Type *i64 = llvm::Type::getInt64Ty(context);
      llvm::Constant* values[2] = {
	llvm::ConstantInt::get(i64, 0),
	llvm::ConstantInt::get(i64, 1)
      };

      return LLVMConstantBuilder::constant_value(llvm::ConstantStruct::get(context, values, 2, false));
    }

    LLVMConstantBuilder::Constant Metatype::llvm_value(llvm::Constant *size, llvm::Constant *align) {
      llvm::LLVMContext& context = size->getContext();
      PSI_ASSERT(size->getType()->isIntegerTy(64) && align->getType()->isIntegerTy(64),
		 "size and align members of Metatype must both be i64");
      PSI_ASSERT(!llvm::cast<llvm::ConstantInt>(align)->equalsInt(0), "align cannot be zero");
      llvm::Constant* values[2] = {size, align};
      return LLVMConstantBuilder::constant_value(llvm::ConstantStruct::get(context, values, 2, false));
    }

    LLVMFunctionBuilder::Result Metatype::llvm_value(LLVMFunctionBuilder& builder, llvm::Value *size, llvm::Value *align) {
      LLVMFunctionBuilder::IRBuilder& irbuilder = builder.irbuilder();
      llvm::LLVMContext& context = builder.context();
      const llvm::Type* i64 = llvm::Type::getInt64Ty(context);
      llvm::Type *mtype = llvm::StructType::get(context, i64, i64, NULL);
      llvm::Value *first = irbuilder.CreateInsertValue(llvm::UndefValue::get(mtype), size, 0);
      llvm::Value *second = irbuilder.CreateInsertValue(first, align, 1);
      return LLVMFunctionBuilder::make_known(second);
    }
#endif

#if 0
    GlobalVariable::GlobalVariable(bool read_only) : m_read_only(read_only) {
    }

    Term* GlobalVariable::create(bool read_only, Term *value) {
      return value->context().new_term(GlobalVariable(read_only), value->type(), value);
    }

    Term* GlobalVariable::type(Context&, std::size_t n_parameters, Term *const* parameters) const {
      if (n_parameters != 2)
	throw std::logic_error("Global variable takes two parameters: the variable type and the variable value");

      if (parameters[0]->proto().category() == term_value)
	throw std::logic_error("type parameter to global variable is a value");

      if (parameters[1]) {
	if (parameters[0] != parameters[1]->type())
	  throw std::logic_error("type of second parameter to global is not first parameter");
      }

      return PointerType::create(parameters[0]);
    }

    bool GlobalVariable::equals_internal(const ProtoTerm& other) const {
      return m_read_only == static_cast<const GlobalVariable&>(other).m_read_only;
    }

    std::size_t GlobalVariable::hash_internal() const {
      return HashCombiner() << m_read_only;
    }

    ProtoTerm* GlobalVariable::clone() const {
      return new GlobalVariable(*this);
    }

    llvm::GlobalValue* GlobalVariable::llvm_build_global(LLVMConstantBuilder& builder, Term* term) const {
      Term *type = term->parameter(0);
      LLVMConstantBuilder::Type llvm_type = builder.type(type);
      if (llvm_type.known()) {
	return new llvm::GlobalVariable(builder.module(), llvm_type.type(), m_read_only,
					llvm::GlobalValue::ExternalLinkage,
					NULL, "");
      } else if (llvm_type.empty()) {
	const llvm::Type *i8 = llvm::Type::getInt8Ty(builder.context());
	llvm::Constant *v = llvm::ConstantInt::get(i8, 0);
	return new llvm::GlobalVariable(builder.module(), i8, true,
					llvm::GlobalValue::ExternalLinkage,
					v, "");
      } else {
	throw std::logic_error("Type of a global variable must be known (or empty)");
      }
    }

    void GlobalVariable::llvm_init_global(LLVMConstantBuilder& builder, llvm::GlobalValue *llvm_global, Term* term) const {
      llvm::GlobalVariable *gv = llvm::cast<llvm::GlobalVariable>(llvm_global);
      Term *initializer = term->parameter(1);
      LLVMConstantBuilder::Constant init_llvm = builder.constant(initializer);
      if (init_llvm.empty()) {
	PSI_ASSERT(gv->getInitializer(), "Initializer for empty global is null");
      } else {
	gv->setInitializer(init_llvm.value());
      }
    }
#endif

    FunctionalBaseTerm::FunctionalBaseTerm(const UserInitializer& ui, Context *context,
					   TermType term_type, bool complete, Term *type,
					   std::size_t hash)
      : Term(ui, context, term_type, complete, type), m_hash(hash) {
    }

    bool FunctionalBaseTerm::check_complete(Term *type, std::size_t n_parameters, Term *const* parameters) {
      if (!type->complete())
	return false;
      for (std::size_t i = 0; i < n_parameters; ++i) {
	if (!parameters[i]->complete())
	  return false;
      }
      return true;
    }

    FunctionalTerm::FunctionalTerm(const UserInitializer& ui, Context *context, Term *type,
				   std::size_t hash, FunctionalTermBackend *backend,
				   std::size_t n_parameters, Term *const* parameters)
      : FunctionalBaseTerm(ui, context, term_functional,
			   check_complete(type, n_parameters, parameters), type, hash),
	m_backend(backend) {
      for (std::size_t i = 0; i < n_parameters; ++i)
	set_parameter(i, parameters[i]);
    }

    FunctionalTerm::~FunctionalTerm() {
      m_backend->~FunctionalTermBackend();
    }

    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui, Context *context, std::size_t hash,
				       Term *result_type, std::size_t n_parameters, Term *const* parameter_types)
      : FunctionalBaseTerm(ui, context, term_function_type,
			   check_complete(result_type, n_parameters, parameter_types),
			   context->get_metatype(), hash) {
    }

    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context,
							 Term *type, std::size_t hash, Term *source, std::size_t index)
      : FunctionalBaseTerm(ui, context, term_function_type_parameter,
			   type->complete() && source->complete(),
			   type, hash),
	m_index(index) {
      set_parameter(0, source);
    }

    OpaqueResolverTerm::OpaqueResolverTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term *type, std::size_t depth)
      : FunctionalBaseTerm(ui, context, term_opaque_resolver, false, type, hash),
	m_depth(depth) {
    }
  }
}
