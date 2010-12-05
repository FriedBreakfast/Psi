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

    void TermUser::resize_uses(std::size_t n) {
      User::resize_uses(n);
    }

    PersistentTermPtr::PersistentTermPtr()
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
    }

    PersistentTermPtr::PersistentTermPtr(const PersistentTermPtr& src)
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
      reset(src.get());
    }

    PersistentTermPtr::PersistentTermPtr(Term *ptr)
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
      reset(ptr);
    }

    PersistentTermPtr::~PersistentTermPtr() {
      reset(0);
    }

    void PersistentTermPtr::reset(Term *term) {
      use_set(0, term);
    }

    const PersistentTermPtr& PersistentTermPtr::operator = (const PersistentTermPtr& o) {
      reset(o.get());
      return *this;
    }

    Term::Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool phantom, Term *source, Term* type)
      : TermUser(ui, term_type),
        m_abstract(abstract),
        m_parameterized(parameterized),
        m_phantom(phantom),
        m_context(context),
        m_source(source) {

      PSI_ASSERT(!source || (source->term_type() == term_function) || (source->term_type() == term_block));

      if (!type) {
        if (term_type != term_recursive) {
          m_category = category_metatype;
          PSI_ASSERT(term_type == term_functional);
        } else {
          m_category = category_recursive;
        }
      } else {
	if (context != type->m_context)
	  throw std::logic_error("context mismatch between term and its type");

        switch (type->m_category) {
        case category_metatype: m_category = category_type; break;
        case category_type: m_category = category_value; break;

        default:
          throw std::logic_error("type of a term cannot be a value or recursive, it must be metatype or a type");
        }
      }

      use_set(0, type);
    }

    Term::~Term() {
      std::size_t n = n_uses();
      for (std::size_t i = 0; i < n; ++i)
	use_set(i, 0);
    }

    void Term::set_base_parameter(std::size_t n, Term *t) {
      if (t && (m_context != t->m_context))
        throw std::logic_error("term context mismatch");

      PSI_ASSERT_MSG(!use_get(n+1), "parameters to existing terms cannot be changed once set");
      PSI_ASSERT(!t
                 || ((term_type() == term_function)
                     && ((t->term_type() == term_function_parameter)
                         || (t->term_type() == term_block)))
                 || source_dominated(t->source(), source()));

      use_set(n+1, t);
    }

    /**
     * Change the number of parameters to this term. This should only
     * be used for phi terms.
     */
    void Term::resize_base_parameters(std::size_t n) {
      PSI_ASSERT(term_type() == term_phi);
      resize_uses(n+1);
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

    HashTerm::HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool phantom, Term *source, Term* type, std::size_t hash)
      : Term(ui, context, term_type, abstract, parameterized, phantom, source, type),
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
    GlobalTerm::GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, Term* type, const std::string& name)
      : Term(ui, context, term_type, false, false, false, NULL, context->get_pointer_type(type).get()),
        m_name(name) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
    }

    /**
     * \brief Get the value type of a global, i.e. the type pointed to
     * by the global's value.
     */
    Term* GlobalTerm::value_type() const {
      FunctionalTerm* ft = checked_cast<FunctionalTerm*>(type());
      return checked_cast_functional<PointerType>(ft).backend().target_type();
    }

    GlobalVariableTerm::GlobalVariableTerm(const UserInitializer& ui, Context *context, Term* type,
                                           bool constant, const std::string& name)
      : GlobalTerm(ui, context, term_global_variable, type, name),
	m_constant(constant) {
    }

    void GlobalVariableTerm::set_value(Term* value) {
      if (!value->global())
	throw std::logic_error("value of global variable must be a global");

      set_base_parameter(0, value);
    }

    class GlobalVariableTerm::Initializer : public InitializerBase<GlobalVariableTerm> {
    public:
      Initializer(Term* type, bool constant, const std::string& name)
	: m_type(type), m_constant(constant), m_name(name) {
      }

      GlobalVariableTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) GlobalVariableTerm(ui, context, m_type, m_constant, m_name);
      }

      std::size_t n_uses() const {
	return 1;
      }

    private:
      Term* m_type;
      bool m_constant;
      std::string m_name;
    };

    /**
     * \brief Create a new global term.
     */
    GlobalVariableTerm* Context::new_global_variable(Term* type, bool constant, const std::string& name) {
      return allocate_term(GlobalVariableTerm::Initializer(type, constant, name));
    }

    /**
     * \brief Create a new global term, initialized with the specified value.
     */
    GlobalVariableTerm* Context::new_global_variable_set(Term* value, bool constant, const std::string& name) {
      GlobalVariableTerm* t = new_global_variable(value->type(), constant, name);
      t->set_value(value);
      return t;
    }

    /**
     * \brief Just-in-time compile a term, and a get a pointer to
     * the result.
     */
    void* Context::term_jit(GlobalTerm* term) {
      if ((term->m_term_type != term_global_variable) &&
	  (term->m_term_type != term_function))
	throw std::logic_error("Cannot JIT compile non-global term");

      if (!m_llvm_context) {
	m_llvm_context.reset(new llvm::LLVMContext());
	m_llvm_module.reset(new llvm::Module("", *m_llvm_context));
      }

      LLVMConstantBuilder builder(m_llvm_context.get(), m_llvm_module.get());
      llvm::GlobalValue *global = builder.global(term);

      if (!m_llvm_engine) {
	llvm::InitializeNativeTarget();
	m_llvm_engine.reset(llvm::EngineBuilder(m_llvm_module.release()).create());
	PSI_ASSERT_MSG(m_llvm_engine.get(), "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");

        // insert event listeners
        for (std::tr1::unordered_set<llvm::JITEventListener*>::iterator it = m_llvm_jit_listeners.begin();
             it != m_llvm_jit_listeners.end(); ++it) {
          m_llvm_engine->RegisterJITEventListener(*it);
        }

        std::tr1::unordered_set<llvm::JITEventListener*>().swap(m_llvm_jit_listeners);
      } else {
	m_llvm_engine->addModule(m_llvm_module.release());
      }

      m_llvm_module.reset(new llvm::Module("", *m_llvm_context));

      return m_llvm_engine->getPointerToGlobal(global);
    }

    void Context::register_llvm_jit_listener(llvm::JITEventListener *l) {
      if (m_llvm_engine) {
        m_llvm_engine->RegisterJITEventListener(l);
      } else {
        m_llvm_jit_listeners.insert(l);
      }
    }

    void Context::unregister_llvm_jit_listener(llvm::JITEventListener *l) {
      if (m_llvm_engine) {
        m_llvm_engine->UnregisterJITEventListener(l);
      } else {
        m_llvm_jit_listeners.erase(l);
      }
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
    bool term_unique(Term* term) {
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
