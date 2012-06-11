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
                 Value *source, const SourceLocation& location)
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

    std::size_t Value::hash_value() const {
      if (const HashableValue *ht = dyn_cast<HashableValue>(this))
        return ht->m_hash;
      else
        return boost::hash_value(this);
    }
    
    /**
     * Dump this term to stderr.
     */
    void Value::dump() {
      print_term(std::cerr, ValuePtr<>(this));
    }
    
    std::size_t Context::HashableValueHasher::operator() (const HashableValue& h) const {
      return h.m_hash;
    }

    HashableValue::HashableValue(Context *context, TermType term_type, const ValuePtr<>& type, std::size_t hash,
                                 Value *source, const SourceLocation& location)
    : Value(context, term_type, type, source, location),
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
    Global::Global(Context *context, TermType term_type, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location)
    : Value(context, term_type, PointerType::get(type, location), this, location),
      m_name(name),
      m_module(module) {
    }

    /**
     * \brief Get the value type of a global, i.e. the type pointed to
     * by the global's value.
     */
    ValuePtr<> Global::value_type() const {
      return value_cast<PointerType>(type())->target_type();
    }

    GlobalVariable::GlobalVariable(Context *context, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location)
    : Global(context, term_global_variable, type, name, module, location),
      m_constant(false) {
    }

    void GlobalVariable::set_value(const ValuePtr<>& value) {
      if (value->phantom())
        throw TvmUserError("value of global variable cannot be phantom");

      if (value->source()) {
        Global *source = dyn_cast<Global>(value->source());
        if (!source || (module() != source->module()))
          throw TvmUserError("value of global variable must be a global from the same module");
      }

      m_value = value;
    }
    
    /**
     * \brief Create a new global term.
     */
    ValuePtr<GlobalVariable> Module::new_global_variable(const std::string& name, const ValuePtr<>& type, const SourceLocation& location) {
      if (type->phantom())
        throw TvmUserError("global variable type cannot be phantom");

      ValuePtr<GlobalVariable> result(::new GlobalVariable(&context(), type, name, this, location));
      add_member(result);
      return result;
    }

    /**
     * \brief Create a new global term, initialized with the specified value.
     */
    ValuePtr<GlobalVariable> Module::new_global_variable_set(const std::string& name, const ValuePtr<>& value, const SourceLocation& location) {
      ValuePtr<GlobalVariable> t = new_global_variable(name, value->type(), location);
      t->set_value(value);
      return t;
    }

    Context::Context()
      : m_hash_term_buckets(initial_hash_term_buckets),
        m_hash_terms(HashTermSetType::bucket_traits(m_hash_term_buckets.get(), initial_hash_term_buckets)) {
    }

    struct Context::ValueDisposer {
      void operator () (Value *t) const {
        t->gc_clear();
        operator delete (t);
      }
    };

    Context::~Context() {
      m_all_terms.clear_and_dispose(ValueDisposer());
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

    std::size_t Module::GlobalHasher::operator() (const Global& h) const {
      return boost::hash_value(h.name());
    }

    bool Module::GlobalEquals::operator () (const Global& lhs, const Global& rhs) const {
      return lhs.name() == rhs.name();
    }

    /**
     * \brief Module constructor.
     * 
     * \param name Name of the module.
     */
    Module::Module(Context *context, const std::string& name, const SourceLocation& location)
    : m_context(context),
    m_location(location),
    m_name(name),
    m_members_buckets(initial_members_buckets),
    m_members(ModuleMemberList::bucket_traits(m_members_buckets.get(), initial_members_buckets)) {
    }
    
    Module::~Module() {
    }

    void Module::add_member(const ValuePtr<Global>& term) {
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
    bool term_unique(const ValuePtr<>& term) {
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
