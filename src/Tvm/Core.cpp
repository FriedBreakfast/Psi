#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"
#include "LLVMBuilder.hpp"

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
      Term *old = get();
      if (term != old) {
	if (old) {
	  old->term_release();
	}

	if (term) {
	  term->term_add_ref();
	  use_set(0, term);
	}
      }
    }

    Term::Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, TermRef<> type)
      : TermUser(ui, term_type),
        m_abstract(abstract),
        m_parameterized(parameterized),
        m_global(global),
        m_use_count_ptr(0),
        m_context(context) {

      m_use_count.value = 0;

      if (type.get()) {
	if (context != type->m_context)
	  throw std::logic_error("context mismatch between term and its type");
        type->term_add_ref();
      }

      use_set(0, type.get());
    }

    Term::~Term() {
      std::size_t n = n_uses();
      for (std::size_t i = 0; i < n; ++i)
	use_set(i, 0);
    }

    void Term::set_base_parameter(std::size_t n, TermRef<> t) {
      if (m_context != t->m_context)
        throw std::logic_error("term context mismatch");

      Term *old = use_get(n+1);
      if (t.get() == old)
        return;

      use_set(n+1, t.get());

      if (t.get())
        t->term_add_ref();
      if (old)
        t->term_release();
    }

    std::size_t Term::hash_value() const {
      switch (term_type()) {
      case term_functional:
      case term_function_type_internal:
      case term_function_type_internal_parameter:
	return checked_cast<const HashTerm*>(this)->m_hash;

      default:
	PSI_ASSERT(!dynamic_cast<const HashTerm*>(this));
	return boost::hash_value(this);
      }
    }

    void Term::term_destroy(Term *term) {
      std::tr1::unordered_set<Term*> visited;
      std::vector<Term*> queue;
      visited.insert(term);
      queue.push_back(term);
      while(!queue.empty()) {
	Term *current = queue.back();
	queue.pop_back();
	PSI_ASSERT(!*current->term_use_count());

	for (TermIterator<Term> it = current->term_users_begin<Term>();
	     it != current->term_users_end<Term>(); ++it) {
          // any terms refering to this one should be part of the same
          // cyclic structure
          PSI_ASSERT(current->term_use_count() == it->term_use_count());
	  if (visited.insert(&*it).second)
	    queue.push_back(&*it);
	}

	std::size_t n_uses = current->n_uses();
	for (std::size_t i = 0; i < n_uses; ++i) {
          if (Term *child = current->use_get(i)) {
            std::size_t *child_use_count = child->term_use_count();
            if (!*child_use_count || !--*child_use_count) {
              if (visited.insert(child).second)
                queue.push_back(child);
            }
          }
	}

	current->clear_users();

	delete current;
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
      return TermPtr<GlobalVariableTerm>(allocate_term(this, GlobalVariableTerm::Initializer(type.get(), constant)));
    }

    /**
     * \brief Create a new global term, initialized with the specified value.
     */
    TermPtr<GlobalVariableTerm> Context::new_global_variable_set(TermRef<> value, bool constant) {
      TermPtr<GlobalVariableTerm> t = new_global_variable(value->type(), constant);
      t->set_value(value);
      return t;
    }

    namespace {
      void insert_if_abstract(std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set, TermRef<> term) {
	if (term->abstract()) {
	  if (set.insert(term.get()).second)
	    queue.push_back(term.get());
	}
      }
    }

    /**
     * \brief Deep search a term to determine whether it is really
     * abstract.
     */
    bool Context::search_for_abstract(Term *term, std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set) {
      if (!term->abstract())
	return false;

      PSI_ASSERT(queue.empty() && set.empty());
      queue.push_back(term);
      set.insert(term);
      while(!queue.empty()) {
	Term *term = queue.back();
	queue.pop_back();

	PSI_ASSERT(term->abstract());

	insert_if_abstract(queue, set, term->type());

	switch (term->term_type()) {
	case term_functional: {
	  FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(*term);
	  for (std::size_t i = 0; i < cast_term.n_parameters(); ++i)
	    insert_if_abstract(queue, set, cast_term.parameter(i));
	  break;
	}

	case term_recursive: {
	  RecursiveTerm& cast_term = checked_cast<RecursiveTerm&>(*term);
	  if (!cast_term.result()) {
	    queue.clear();
	    set.clear();
	    return true;
	  }
	  insert_if_abstract(queue, set, cast_term.result());
	  for (std::size_t i = 0; i < cast_term.n_parameters(); i++)
	    insert_if_abstract(queue, set, cast_term.parameter(i)->type());
	  break;
	}

	case term_function_type: {
	  FunctionTypeTerm& cast_term = checked_cast<FunctionTypeTerm&>(*term);
	  insert_if_abstract(queue, set, cast_term.result_type());
	  for (std::size_t i = 0; i < cast_term.n_parameters(); ++i)
	    insert_if_abstract(queue, set, cast_term.parameter(i)->type());
	  break;
	}

	case term_recursive_parameter:
	case term_function_type_parameter: {
	  // Don't need to check these since they're covered by the
	  // function_type and recursive case
	  break;
	}

	default:
	  PSI_FAIL("unexpected abstract term type");
	}
      }

      queue.clear();
      set.clear();
      return false;
    }

    void Context::clear_and_queue_if_abstract(std::vector<Term*>& queue, TermRef<> t) {
      if (t->abstract()) {
	t->m_abstract = false;
	queue.push_back(t.get());
      }
    }

    /**
     * \brief Clear abstract flag in this term and all its
     * descendents.
     *
     * \param queue Vector to use to queue terms to clear. This is an
     * optimization since #resolve_recursive calls this function
     * repeatedly and this saves reallocating queue space. It must be
     * empty on entry to this function.
     */
    void Context::clear_abstract(Term *term, std::vector<Term*>& queue) {
      if (!term->abstract())
	return;

      PSI_ASSERT(queue.empty());
      queue.push_back(term);
      while(!queue.empty()) {
	Term *term = queue.back();
	queue.pop_back();

	switch (term->term_type()) {
	case term_functional: {
	  FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(*term);
	  clear_and_queue_if_abstract(queue, cast_term.type());
	  for (std::size_t i = 0; i < cast_term.n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term.parameter(i));
	  break;
	}

	case term_recursive: {
	  RecursiveTerm& cast_term = checked_cast<RecursiveTerm&>(*term);
	  PSI_ASSERT(cast_term.result());
	  clear_and_queue_if_abstract(queue, cast_term.result());
	  for (std::size_t i = 0; i < cast_term.n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term.parameter(i)->type());
	  break;
	}

	case term_function_type: {
	  FunctionTypeTerm& cast_term = checked_cast<FunctionTypeTerm&>(*term);
	  clear_and_queue_if_abstract(queue, cast_term.result_type());
	  for (std::size_t i = 0; i < cast_term.n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term.parameter(i)->type());
	  break;
	}

	case term_recursive_parameter:
	case term_function_type_parameter: {
	  // Don't need to check these since they're covered by the
	  // function_type and recursive cases
	  break;
	}

	default:
	  PSI_FAIL("unexpected abstract term type");
	}
      }
    }

    /**
     * \brief Resolve an opaque term.
     */
    void Context::resolve_recursive(TermRef<RecursiveTerm> recursive, TermRef<> to) {
      if (recursive->type() != to->type())
	throw std::logic_error("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
	throw std::logic_error("cannot resolve recursive term to parameterized term");

      if (recursive->result())
	throw std::logic_error("resolving a recursive term which has already been resolved");

      recursive->set_base_parameter(1, to.get());

      std::vector<Term*> queue;
      std::tr1::unordered_set<Term*> set;
      if (!search_for_abstract(recursive.get(), queue, set)) {
	recursive->m_abstract = false;

	clear_abstract(recursive.get(), queue);

	std::vector<Term*> upward_queue;
	upward_queue.push_back(recursive.get());
	while (!upward_queue.empty()) {
	  Term *t = upward_queue.back();
	  upward_queue.pop_back();
	  for (TermIterator<Term> it = t->term_users_begin<Term>(); it != t->term_users_end<Term>(); ++it) {
	    if (it->abstract() && !search_for_abstract(&*it, queue, set)) {
	      clear_abstract(&*it, queue);
	      upward_queue.push_back(&*it);
	    }
	  }
	}
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

    /**
     * \brief Just-in-time compile a term, and a get a pointer to
     * the result.
     */
    void* Context::term_jit(TermRef<GlobalTerm> term) {
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
      } else {
	m_llvm_engine->addModule(m_llvm_module.release());
      }

      m_llvm_module.reset(new llvm::Module("", *m_llvm_context));

      return m_llvm_engine->getPointerToGlobal(global);
    }

    Context::Context()
      : m_hash_term_buckets(new HashTermSetType::bucket_type[initial_hash_term_buckets]),
	m_hash_terms(HashTermSetType::bucket_traits(m_hash_term_buckets.get(), initial_hash_term_buckets)) {
    }

    Context::~Context() {
      PSI_WARNING(m_hash_terms.empty());
    }
  }
}
