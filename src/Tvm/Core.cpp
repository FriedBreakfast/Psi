#include "Aggregate.hpp"
#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
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

    Value::Value(Context *context, TermType term_type, const ValuePtr<>& type,
                 const ValuePtr<>& source, const SourceLocation& location)
    : m_reference_count(0),
    m_context(context),
    m_term_type(term_type),
    m_type(type),
    m_source(source),
    m_location(location) {

      PSI_ASSERT(!source ||
        (source->term_type() == term_global_variable) ||
        (source->term_type() == term_function) ||
        (source->term_type() == term_block) ||
        (source->term_type() == term_phi) ||
        (source->term_type() == term_instruction) ||
        (source->term_type() == term_function_type_parameter) ||
        (source->term_type() == term_function_parameter));

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
    }

    Value::~Value() {
    }
    
    void Value::destroy() {
      delete this;
    }

    std::size_t Term::hash_value() const {
      if (HashableValue *ht = dyn_cast<HashableValue>(const_cast<Term*>(this)))
        return ht->m_hash;
      else
        return boost::hash_value(this);
    }
    
    /**
     * Dump this term to stderr.
     */
    void Term::dump() {
      print_term(std::cerr, this);
    }
    
    std::size_t Context::HashTermHasher::operator() (const HashTerm& h) const {
      return h.m_hash;
    }

    HashableValue::HashableValue(Context *context, TermType term_type, const ValuePtr<>& type, std::size_t hash)
    : Value(context, term_type, type),
      m_hash(hash) {
    }

    HashableValue::~HashableValue() {
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
    Global::Global(Context *context, TermType term_type, const ValuePtr<>& type, const std::string& name, Module *module)
    : Value(context, term_type, PointerType::get(type)),
      m_name(name),
      m_module(module) {
    }

    /**
     * \brief Get the value type of a global, i.e. the type pointed to
     * by the global's value.
     */
    Term* GlobalTerm::value_type() const {
      return cast<PointerType>(type())->target_type();
    }

    GlobalVariableTerm::GlobalVariableTerm(const UserInitializer& ui, Context *context, Term* type,
                                           const std::string& name, Module *module)
      : GlobalTerm(ui, context, term_global_variable, type, name, module),
        m_constant(false) {
    }

    void GlobalVariableTerm::set_value(Term* value) {
      if (value->phantom())
        throw TvmUserError("value of global variable cannot be phantom");

      if (value->source()) {
        GlobalTerm *source = dyn_cast<GlobalTerm>(value->source());
        if (!source || (module() != source->module()))
          throw TvmUserError("value of global variable must be a global from the same module");
      }

      set_base_parameter(1, value);
    }
    
    class GlobalVariableTerm::Initializer : public InitializerBase<GlobalVariableTerm> {
    public:
      Initializer(Term* type, const std::string& name, Module *module)
        : m_type(type), m_name(name), m_module(module) {
      }

      GlobalVariableTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
        return new (base) GlobalVariableTerm(ui, context, m_type, m_name, m_module);
      }

      std::size_t n_uses() const {
        return 2;
      }

    private:
      Term* m_type;
      std::string m_name;
      Module *m_module;
    };

    /**
     * \brief Create a new global term.
     */
    GlobalVariableTerm* Module::new_global_variable(const std::string& name, Term* type) {
      if (type->phantom())
        throw TvmUserError("global variable type cannot be phantom");

      GlobalVariableTerm *result = context().allocate_term(GlobalVariableTerm::Initializer(type, name, this));
      add_member(result);
      return result;
    }

    /**
     * \brief Create a new global term, initialized with the specified value.
     */
    GlobalVariableTerm* Module::new_global_variable_set(const std::string& name, Term* value) {
      GlobalVariableTerm* t = new_global_variable(name, value->type());
      t->set_value(value);
      return t;
    }

    Context::Context()
      : m_hash_term_buckets(initial_hash_term_buckets),
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

#ifdef PSI_DEBUG
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

    std::size_t Module::GlobalTermHasher::operator() (const GlobalTerm& h) const {
      return boost::hash_value(h.name());
    }

    bool Module::GlobalTermEquals::operator () (const GlobalTerm& lhs, const GlobalTerm& rhs) const {
      return lhs.name() == rhs.name();
    }

    /**
     * \brief Module constructor.
     * 
     * \param name Name of the module.
     */
    Module::Module(Context *context, const std::string& name)
    : m_context(context),
    m_name(name),
    m_members_buckets(initial_members_buckets),
    m_members(ModuleMemberList::bucket_traits(m_members_buckets.get(), initial_members_buckets)) {
    }
    
    Module::~Module() {
    }

    void Module::add_member(GlobalTerm *term) {
      if (!m_members.insert(*term).second)
        throw TvmUserError("Duplicate module member name");
    }
    
    /**
     * Dump all symbols in this module to stderr.
     */
    void Module::dump() {
      print_module(std::cerr, this);
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
