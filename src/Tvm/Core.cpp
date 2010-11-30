#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"
#include "Utility.hpp"
#include "Derived.hpp"
#include "LLVMBuilder.hpp"

#include <stdexcept>
#include <typeinfo>
#include <iostream>

#include <llvm/LLVMContext.h>
#include <llvm/Type.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/GlobalVariable.h>
#include <llvm/Module.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/raw_os_ostream.h>
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
    TermUser::TermUser(const UserInitializer& ui, TermType term_type)
      : User(ui), m_term_type(term_type) {
    }

    TermUser::~TermUser() {
    }

    PersistentTermPtrBackend::PersistentTermPtrBackend()
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
    }

    PersistentTermPtrBackend::PersistentTermPtrBackend(const PersistentTermPtrBackend& src)
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
      reset(src.get());
    }

    PersistentTermPtrBackend::PersistentTermPtrBackend(Term *ptr)
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
      reset(ptr);
    }

    PersistentTermPtrBackend::~PersistentTermPtrBackend() {
      reset(0);
    }

    void PersistentTermPtrBackend::reset(Term *term) {
      use_set(0, term);
    }

    Term::Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, TermRef<> type)
      : TermUser(ui, term_type),
        m_abstract(abstract),
        m_parameterized(parameterized),
        m_global(global),
        m_context(context) {

      if (!type.get()) {
        m_category = category_metatype;
      } else {
	if (context != type->m_context)
	  throw std::logic_error("context mismatch between term and its type");

        switch (type->m_category) {
        case category_metatype: m_category = category_type; break;
        case category_type: m_category = category_value; break;

        default:
          throw std::logic_error("type of a term cannot be a value, it must be metatype or a type");
        }
      }

      use_set(0, type.get());
    }

    Term::~Term() {
      std::size_t n = n_uses();
      for (std::size_t i = 0; i < n; ++i)
	use_set(i, 0);
    }

    void Term::set_base_parameter(std::size_t n, TermRef<> t) {
      if (t.get() && (m_context != t->m_context))
        throw std::logic_error("term context mismatch");

      Term *old = use_get(n+1);
      if (t.get() == old)
        return;

      use_set(n+1, t.get());
    }

    std::size_t Term::hash_value() const {
      switch (term_type()) {
      case term_functional:
      case term_function_type_resolver:
	return checked_cast<const HashTerm*>(this)->m_hash;

      default:
	PSI_ASSERT(!dynamic_cast<const HashTerm*>(this));
	return boost::hash_value(this);
      }
    }

    std::size_t Context::HashTermHasher::operator() (const HashTerm& h) const {
      return h.m_hash;
    }

    HashTerm::HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, TermRef<> type, std::size_t hash)
      : Term(ui, context, term_type, abstract, parameterized, global, type),
	m_hash(hash) {
    }

    HashTerm::~HashTerm() {
      Context::HashTermSetType& hs = context().m_hash_terms;
      hs.erase(hs.iterator_to(*this));
    }

    /**
     * \brief Global term constructor.
     *
     * The \c type parameter should be the type of the global variable
     * contents; the final type of this variable will in fact be a
     * pointer to this type.
     */
    GlobalTerm::GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, TermRef<> type)
      : Term(ui, context, term_type, false, false, true, context->get_pointer_type(type)) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
    }

    /**
     * \brief Get the value type of a global, i.e. the type pointed to
     * by the global's value.
     */
    TermPtr<> GlobalTerm::value_type() const {
      TermPtr<FunctionalTerm> ft = checked_term_cast<FunctionalTerm>(type());
      return checked_cast_functional<PointerType>(ft).backend().target_type();
    }

    GlobalVariableTerm::GlobalVariableTerm(const UserInitializer& ui, Context *context, TermRef<> type, bool constant)
      : GlobalTerm(ui, context, term_global_variable, type),
	m_constant(constant) {
    }

    void GlobalVariableTerm::set_value(TermRef<> value) {
      if (!value->global())
	throw std::logic_error("value of global variable must be a global");

      set_base_parameter(0, value.get());
    }

    class GlobalVariableTerm::Initializer : public InitializerBase<GlobalVariableTerm> {
    public:
      Initializer(TermRef<> type, bool constant)
	: m_type(type), m_constant(constant) {
      }

      GlobalVariableTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) GlobalVariableTerm(ui, context, m_type, m_constant);
      }

      std::size_t n_uses() const {
	return 1;
      }

    private:
      TermRef<> m_type;
      bool m_constant;
    };

    /**
     * \brief Create a new global term.
     */
    TermPtr<GlobalVariableTerm> Context::new_global_variable(TermRef<> type, bool constant) {
      return allocate_term(GlobalVariableTerm::Initializer(type.get(), constant));
    }

    /**
     * \brief Create a new global term, initialized with the specified value.
     */
    TermPtr<GlobalVariableTerm> Context::new_global_variable_set(TermRef<> value, bool constant) {
      TermPtr<GlobalVariableTerm> t = new_global_variable(value->type(), constant);
      t->set_value(value);
      return t;
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

    /**
     * \brief Just-in-time compile a term, and a get a pointer to
     * the result.
     */
    void* Context::term_jit_internal(TermRef<GlobalTerm> term, llvm::raw_ostream *debug) {
      if ((term->m_term_type != term_global_variable) &&
	  (term->m_term_type != term_function))
	throw std::logic_error("Cannot JIT compile non-global term");

      if (!m_llvm_context) {
	m_llvm_context.reset(new llvm::LLVMContext());
	m_llvm_module.reset(new llvm::Module("", *m_llvm_context));
      }

      LLVMConstantBuilder builder(m_llvm_context.get(), m_llvm_module.get());
      builder.set_debug(debug);
      llvm::GlobalValue *global = builder.global(term);

      if (!m_llvm_engine) {
	llvm::InitializeNativeTarget();
	m_llvm_engine.reset(llvm::EngineBuilder(m_llvm_module.release()).create());
	PSI_ASSERT_MSG(m_llvm_engine.get(), "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
      } else {
	m_llvm_engine->addModule(m_llvm_module.release());
      }

      m_llvm_module.reset(new llvm::Module("", *m_llvm_context));

      return m_llvm_engine->getPointerToGlobal(global);
    }

    void* Context::term_jit(TermRef<GlobalTerm> term) {
      return term_jit_internal(term, NULL);
    }

    /**
     * \brief Just-in-time compile a term, and a get a pointer to
     * the result.
     */
    void* Context::term_jit(TermRef<GlobalTerm> term, std::ostream& debug) {
      llvm::raw_os_ostream llvm_debug(debug);
      return term_jit_internal(term, &llvm_debug);
    }

    Context::Context()
      : m_hash_term_buckets(new HashTermSetType::bucket_type[initial_hash_term_buckets]),
	m_hash_terms(HashTermSetType::bucket_traits(m_hash_term_buckets.get(), initial_hash_term_buckets)) {
    }

    struct Context::TermDisposer {
      void operator () (Term *t) const {
        t->clear_users();
        t->~Term();
        operator delete (t);
      }
    };

    Context::~Context() {
      m_all_terms.clear_and_dispose(TermDisposer());
      PSI_WARNING(m_hash_terms.empty());
    }

#if PSI_DEBUG
    /**
     * Dump the contents of hash_terms to the specified stream.
     */
    void Context::print_hash_terms(std::ostream& output) {
      for (HashTermSetType::iterator it = m_hash_terms.begin(); it != m_hash_terms.end(); ++it)
        output << &*it << '\n';
    }

    /**
     * Dump the contents of the hash_terms table to stderr.
     */
    void Context::dump_hash_terms() {
      print_hash_terms(std::cerr);
    }
#endif

    namespace {
      std::size_t hash_type_info(const std::type_info& ti) {
#if __GXX_MERGED_TYPEINFO_NAMES
	return boost::hash_value(ti.name());
#else
	std::size_t h = 0;
	for (const char *p = ti.name(); *p != '\0'; ++p)
	  boost::hash_combine(h, *p);
	return h;
#endif
      }
    }

    TermBackend::~TermBackend() {
    }

    std::size_t HashTermBackend::hash_value() const {
      std::size_t h = 0;
      boost::hash_combine(h, hash_internal());
      boost::hash_combine(h, hash_type_info(typeid(*this)));
      return h;
    }

    /**
     * Return whether a term is unique, i.e. it is not functional so
     * a copy would be automatically distinct from the original.
     */
    bool term_unique(TermRef<> term) {
      switch (term->term_type()) {
      case term_instruction:
      case term_block:
      case term_global_variable:
      case term_function:
      case term_function_parameter:
      case term_phi:
	return true;

      default:
	return false;
      }
    }
  }
}
