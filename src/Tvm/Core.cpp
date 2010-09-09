#include "Core.hpp"
#include "Derived.hpp"

#include <stdexcept>
#include <typeinfo>

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
    TermParameter::TermParameter(const TermParameterRef& parent)
      : m_parent(parent), m_depth(depth(parent) + 1) {
    }

    TermParameterRef TermParameter::create() {
      return TermParameterRef(new TermParameter(TermParameterRef()));
    }

    TermParameterRef TermParameter::create(const TermParameterRef& parent) {
      return TermParameterRef(new TermParameter(parent));
    }

    Context::Context() {
    }

    Context::~Context() {
      for (TermSet::iterator it = m_term_set.begin(); it != m_term_set.end(); ++it)
	delete *it;

      PSI_ASSERT(m_proto_term_set.empty(), "All terms were destroyed but proto terms survive");
    }

    struct ProtoPtr {
      Context *c;
      ProtoTerm *ptr;

      explicit ProtoPtr(Context *c_, ProtoTerm *t) : c(c_), ptr(t) {}
      ~ProtoPtr() {if (ptr) c->proto_release(ptr);}
    };

    std::size_t Context::TermHasher::operator () (const Term *t) const {
      return t->hash();
    }

    bool Context::TermEquals::operator () (const Term *lhs, const Term *rhs) const {
      if (lhs->proto() != rhs->proto())
	return false;

      if (lhs->n_parameters() != rhs->n_parameters())
	return false;

      // Only need to do a one-level comparison since all further
      // levels should already be unified
      for (std::size_t i = 0; i < lhs->n_parameters(); ++i) {
	if (lhs->parameter(i) != rhs->parameter(i))
	  return false;
      }

      return true;
    }

    Term* Context::new_term(const ProtoTerm& proto, std::size_t n_parameters, Term *const* parameters) {
      // Find deepest context
      TermParameterRef context;
      for (std::size_t i = 0; i < n_parameters; ++i) {
	if (TermParameter::depth(parameters[i]->term_context()) > TermParameter::depth(context))
	  context = parameters[i]->term_context();
      }

      // Check all other contexts are parents of the deepest one
      for (std::size_t i = 0; i < n_parameters; ++i) {
	for (const TermParameterRef *parent = &parameters[i]->term_context();
	     parent; parent = &(*parent)->parent()) {
	  if (*parent == context)
	    goto parent_found;
	}

	throw std::logic_error("Could not select term context since there is no lowest context");

      parent_found:
	continue;
      }

      Term *ty = proto.type(*this, n_parameters, parameters);
      PSI_ASSERT(((proto.category() == ProtoTerm::term_metatype) ? !ty : 
		  (ty && (proto.category() == ty->proto().category() + 1))),
		 "Prototype generated incorrect term type");

      ProtoPtr proto_copy(this, new_proto(proto));

      std::size_t o = (sizeof(Term) + align_of<Use>() - 1) & ~(align_of<Use>() - 1);
      std::size_t s = o + sizeof(Use)*(n_parameters+2);
      void *p = operator new (s);
      Use *pu = static_cast<Use*>(static_cast<void*>(static_cast<char*>(p) + o));
      // This would be a lot faster if we could search the hash
      // table without constructing this object, but C++ doesn't
      // support this.
      UniquePtr<Term> term(new (p) Term (UserInitializer(n_parameters+1, pu), this, context, proto_copy.ptr, ty, n_parameters, parameters));
      proto_copy.ptr = NULL;

      std::pair<TermSet::iterator, bool> r = m_term_set.insert(term.get());
      if (!r.second) {
	term.reset();
	return *r.first;
      } else {
	return term.release();
      }
    }

    std::size_t Context::ProtoTermHasher::operator () (const ProtoTerm *proto) const {
      return proto->hash();
    }

    bool Context::ProtoTermEquals::operator () (const ProtoTerm *lhs, const ProtoTerm *rhs) const {
      return *lhs == *rhs;
    }

    ProtoTerm* Context::new_proto(const ProtoTerm& proto) {
      ProtoTermSet::iterator it = m_proto_term_set.find(const_cast<ProtoTerm*>(&proto));
      if (it != m_proto_term_set.end()) {
	++(*it)->m_n_uses;
	return *it;
      }

      UniquePtr<ProtoTerm> copy(proto.clone());
      std::pair<ProtoTermSet::iterator, bool> r = m_proto_term_set.insert(copy.get());
      PSI_ASSERT(r.second, "prototype term inserted into context set between test and copy - concurrency error?");
      ++copy->m_n_uses;
      return copy.release();
    }

    void Context::proto_release(ProtoTerm *proto) {
      if (--proto->m_n_uses == 0) {
	m_proto_term_set.erase(proto);
	delete proto;
      }
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
      LLVMConstantBuilder builder(m_llvm_context.get(), m_llvm_module.get());
      LLVMConstantBuilder::Constant value = builder.constant(term);
      PSI_ASSERT(value.known(), "Cannot JIT compile a value which does not have a known type");
      const llvm::GlobalValue *global = llvm::cast<llvm::GlobalValue>(value.value());

      if (!m_llvm_engine) {
	llvm::InitializeNativeTarget();
	m_llvm_engine.reset(llvm::EngineBuilder(m_llvm_module.release()).create());
	PSI_ASSERT(m_llvm_engine.get(), "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
      } else {
	m_llvm_engine->addModule(m_llvm_module.release());
      }

      m_llvm_module.reset(new llvm::Module("", *m_llvm_context));

      return m_llvm_engine->getPointerToGlobal(global);
    }

    Term::Term(const UserInitializer& ui, Context *context,
	       const TermParameterRef& term_context, ProtoTerm *proto_term,
	       Term *type, std::size_t n_parameters, Term *const* parameters)
      : User(ui), m_context(context), m_term_context(term_context),
	m_proto_term(proto_term) {
      PSI_ASSERT(use_slots() == n_parameters + 1, "Incorrect number of slots allocated");

      if (proto_term->source() == ProtoTerm::term_functional) {
	m_hash = proto_term->hash();
	for (std::size_t n = 0; n < n_parameters; ++n)
	  m_hash = m_hash*526 + parameters[n]->hash();
      } else {
	m_hash = std::tr1::hash<void*>()(this);
      }

      use_set(0, type);
      for (std::size_t i = 0; i < n_parameters; ++i)
	use_set(i+1, parameters[i]);
    }

    Term::~Term() {
      m_context->proto_release(m_proto_term);
    }

    void Term::set_value(Term *value) {
      switch (proto().source()) {
      case ProtoTerm::term_global:
      case ProtoTerm::term_recursive:
	PSI_ASSERT(n_parameters() == 2, "wrong number of parameters for global or recursive term");
	use_set(2, value);
	break;
      
      default:
	throw std::logic_error("set_value called on term which is not global or recursive");
      }
    }

    ProtoTerm::ProtoTerm(Category category, Source source)
      : m_n_uses(0), m_category(category), m_source(source) {
    }

    ProtoTerm::~ProtoTerm() {
    }

    bool ProtoTerm::operator == (const ProtoTerm& other) const {
      if (typeid(*this) != typeid(other))
	return false;

      return equals_internal(other);
    }

    bool ProtoTerm::operator != (const ProtoTerm& other) const {
      return !(*this == other);
    }

    std::size_t ProtoTerm::hash() const {
      const std::type_info& ti = typeid(*this);
      HashCombiner hc(hash_internal());
      // Good old GCC specific hack. See <typeinfo>
#if __GXX_MERGED_TYPEINFO_NAMES
      hc << std::tr1::hash<const char*>()(ti.name());
#else
      for (const char *p = ti.name(); *p != '\0'; ++p)
	hc << *p;
#endif

      return hc;
    }

    Term* Metatype::create(Context& con) {
      return con.new_term(Metatype());
    }

    Metatype::Metatype() : ProtoTerm(term_metatype, term_functional) {
    }

    Term* Metatype::type(Context&, std::size_t, Term *const*) const {
      return NULL;
    }

    std::size_t Metatype::hash_internal() const {
      return 0;
    }

    bool Metatype::equals_internal(const ProtoTerm&) const {
      return true;
    }

    ProtoTerm* Metatype::clone() const {
      return new Metatype(*this);
    }

    LLVMFunctionBuilder::Result Metatype::llvm_value_instruction(LLVMFunctionBuilder&, Term*) const {
      throw std::logic_error("Metatype does not have a value");
    }

    LLVMConstantBuilder::Constant Metatype::llvm_value_constant(LLVMConstantBuilder&, Term*) const {
      throw std::logic_error("Metatype does not have a value");
    }

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

    Type::Type(Source source) : ProtoTerm(term_type, source) {
    }

    Term* Type::type(Context& context, std::size_t, Term *const*) const {
      return Metatype::create(context);
    }

    PrimitiveType::PrimitiveType() : Type(term_functional) {
    }

    LLVMFunctionBuilder::Result PrimitiveType::llvm_value_instruction(LLVMFunctionBuilder&, Term*) const {
      throw std::logic_error("primitive value should not be generated by instruction");
    }

    Value::Value(Source source) : ProtoTerm(term_value, source) {
    }

    LLVMConstantBuilder::Type Value::llvm_type(LLVMConstantBuilder&, Term*) const {
      throw std::logic_error("llvm_type should never be called on value instances");
    }

    ConstantValue::ConstantValue(Source source) : Value(source) {
    }

    LLVMFunctionBuilder::Result ConstantValue::llvm_value_instruction(LLVMFunctionBuilder&, Term*) const {
      throw std::logic_error("constant value should not be generated by instruction");
    }

    GlobalVariable::GlobalVariable(bool read_only)
      : ConstantValue(term_global), m_read_only(read_only) {
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

    LLVMConstantBuilder::Constant GlobalVariable::llvm_value_constant(LLVMConstantBuilder& builder, Term* term) const {
      LLVMConstantBuilder::Type llvm_type = builder.type(term->type());
      if (llvm_type.known()) {
	//LLVMBuilderValue v = builder.value(initializer());
	//PSI_ASSERT(llvm::isa<llvm::Constant>(v.value()), "Global initializer is not constant");
	return LLVMConstantBuilder::constant_value
	  (new llvm::GlobalVariable(builder.module(), llvm_type.type(), m_read_only,
				    llvm::GlobalValue::ExternalLinkage,
				    NULL,//const_cast<llvm::Constant*>(llvm::cast<llvm::Constant>(v.value())),
				    ""));
      } else if (llvm_type.empty()) {
	const llvm::Type *i8 = llvm::Type::getInt8Ty(builder.context());
	llvm::Constant *v = llvm::ConstantInt::get(i8, 0);
	return LLVMConstantBuilder::constant_value
	  (new llvm::GlobalVariable(builder.module(), i8, true,
				    llvm::GlobalValue::ExternalLinkage,
				    v, ""));
      } else {
	throw std::logic_error("Type of a global variable must be known (or empty)");
      }
    }
  }
}
