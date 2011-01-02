#include "Aggregate.hpp"
#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"
#include "Utility.hpp"

#include <iostream>

namespace Psi {
  namespace Tvm {
    TvmUserError::TvmUserError(const std::string& msg) {
      m_message = "Psi TVM user error: ";
      m_message += msg;
      m_str = m_message.c_str();
    }

    TvmUserError::~TvmUserError() throw () {
    }

    const char* TvmUserError::what() const throw() {
      return m_str;
    }

    TvmInternalError::TvmInternalError(const std::string& msg) {
      m_message = "Psi TVM internal error: ";
      m_message += msg;
      m_str = m_message.c_str();
    }

    TvmInternalError::~TvmInternalError() throw () {
    }

    const char* TvmInternalError::what() const throw() {
      return m_str;
    }

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

      PSI_ASSERT(!source || (source->term_type() == term_function) || (source->term_type() == term_block) || (source->term_type() == term_instruction));

      if (!type) {
        if (term_type != term_recursive) {
          m_category = category_metatype;
          PSI_ASSERT(term_type == term_functional);
        } else {
          m_category = category_recursive;
        }
      } else {
	if (context != type->m_context)
	  throw TvmUserError("context mismatch between term and its type");

        switch (type->m_category) {
        case category_metatype: m_category = category_type; break;
        case category_type: m_category = category_value; break;

        default:
          throw TvmInternalError("type of a term cannot be a value or recursive, it must be metatype or a type");
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
        throw TvmUserError("term context mismatch");

      PSI_ASSERT_MSG(!use_get(n+1), "parameters to existing terms cannot be changed once set");
      PSI_ASSERT(!t
                 || (term_type() == term_function)
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
      : Term(ui, context, term_type, false, false, false, NULL, PointerType::get(type)),
        m_name(name) {
      PSI_ASSERT(!type->phantom() && !type->parameterized() && !type->abstract());
    }

    /**
     * \brief Get the value type of a global, i.e. the type pointed to
     * by the global's value.
     */
    Term* GlobalTerm::value_type() const {
      return cast<PointerType>(type())->target_type();
    }

    GlobalVariableTerm::GlobalVariableTerm(const UserInitializer& ui, Context *context, Term* type,
                                           bool constant, const std::string& name)
      : GlobalTerm(ui, context, term_global_variable, type, name),
	m_constant(constant) {
    }

    void GlobalVariableTerm::set_value(Term* value) {
      if (!value->global())
	throw TvmUserError("value of global variable must be a global");

      if (value->phantom() || value->abstract() || value->parameterized())
        throw TvmUserError("value of global variable cannot be phantom, abstract or parameterized");

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
      if (type->phantom() || type->abstract() || type->parameterized())
        throw TvmUserError("global variable type cannot be phantom, abstract or parameterized");

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
