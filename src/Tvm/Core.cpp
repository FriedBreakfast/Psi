#include "Aggregate.hpp"
#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Recursive.hpp"
#include "Utility.hpp"

#ifdef PSI_DEBUG
#include <iostream>
#endif

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

    Value::Value(Context& context, TermType term_type, const ValuePtr<>& type, const SourceLocation& location)
    : m_reference_count(0),
    m_context(&context),
    m_term_type(term_type),
    m_type(type),
    m_location(location) {
      PSI_ASSERT(m_context);

      if (!type) {
        if (term_type == term_recursive) {
          m_category = category_recursive;
        } else {
          PSI_ASSERT((term_type == term_functional) || (term_type == term_apply) || (term_type == term_function_type) || (term_type == term_exists));
          m_category = category_undetermined;
        }
      } else {
        if (m_context != type->m_context)
          throw TvmUserError("context mismatch between term and its type");

        PSI_ASSERT(type->m_category != category_undetermined);
        switch (type->m_category) {
        case category_metatype: m_category = category_type; break;
        case category_type: m_category = category_value; break;

        default:
          throw TvmInternalError("type of a term cannot be a value or recursive, it must be metatype or a type");
        }
      }
      
      if (m_category != category_undetermined)
        context.m_value_list.push_back(*this);
    }

    Value::~Value() {
      if (m_value_list_hook.is_linked())
        m_context->m_value_list.erase(m_context->m_value_list.iterator_to(*this));
    }

    /**
     * \brief Set the type of this value.
     * 
     * This should only be used for values about to be moved onto the heap.
     */
    void Value::set_type(const ValuePtr<>& type) {
      PSI_ASSERT(m_category == category_undetermined);
      PSI_ASSERT(!m_type);

      if (!type) {
        PSI_ASSERT(m_term_type == term_functional);
        m_category = category_metatype;
      } else if (type->category() == category_metatype) {
        m_category = category_type;
      } else {
        PSI_ASSERT(type->category() == category_type);
        m_category = category_value;
      }

      m_type = type;
      m_context->m_value_list.push_back(*this);
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
     * \brief Check that an operation can be used with a given source
     * 
     * \param source Point at which this value is required to be available.
     * This must be an Instruction, in which case all values generated prior
     * to that instruction are available, a Block, in which case all values generated
     * prior to and during that block are available.
     */
    void Value::check_source(CheckSourceParameter& parameter) {
      if (parameter.available.find(this) != parameter.available.end())
        return;
      check_source_hook(parameter);
      parameter.available.insert(this);
    }
    
#ifdef PSI_DEBUG
    /**
     * Dump this term to stderr.
     */
    void Value::dump() {
      print_term(std::cerr, ValuePtr<>(this));
    }
#endif
    
    std::size_t Context::HashableValueHasher::operator() (const HashableValue& h) const {
      return h.m_hash;
    }

    HashableValue::HashableValue(Context& context, TermType term_type, const SourceLocation& location)
    : Value(context, term_type, ValuePtr<>(), location),
    m_hash(0),
    m_operation(NULL) {
    }
    
    HashableValue::HashableValue(const HashableValue& src)
    : Value(src.context(), src.term_type(), ValuePtr<>(), src.location()),
    m_hash(src.m_hash),
    m_operation(src.m_operation) {
    }

    HashableValue::~HashableValue() {
      if (m_hashable_set_hook.is_linked()) {
        Context::HashTermSetType& hs = context().m_hash_value_set;
        hs.erase(hs.iterator_to(*this));
      }
    }

    /**
     * \brief Global term constructor.
     *
     * The \c type parameter should be the type of the global variable
     * contents; the final type of this variable will in fact be a
     * pointer to this type.
     */
    Global::Global(Context& context, TermType term_type, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location)
    : Value(context, term_type, FunctionalBuilder::pointer_type(type, location), location),
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
    
    /**
     * Returns this object, because global values are their own source.
     */
    Value* Global::disassembler_source() {
      return this;
    }
    
    /**
     * \internal No-op, because globals are visible everywhere.
     */
    void Global::check_source_hook(CheckSourceParameter& parameter) {
      Module *source_module;
      switch (parameter.mode) {
      case CheckSourceParameter::mode_after_block:
      case CheckSourceParameter::mode_before_block:
        source_module = value_cast<Block>(parameter.point)->function_ptr()->module();
        break;
        
      case CheckSourceParameter::mode_before_instruction:
        source_module = value_cast<Instruction>(parameter.point)->block_ptr()->function_ptr()->module();
        break;
        
      case CheckSourceParameter::mode_global:
        source_module = value_cast<Global>(parameter.point)->module();
        break;
        
      default: PSI_FAIL("unexpected enum value");
      }
      
      if (module() != source_module)
        throw TvmUserError("Cannot mix global variables between modules");
    }

    GlobalVariable::GlobalVariable(Context& context, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location)
    : Global(context, term_global_variable, type, name, module, location),
      m_constant(false) {
    }

    void GlobalVariable::set_value(const ValuePtr<>& value) {
      CheckSourceParameter cp(CheckSourceParameter::mode_global, this);
      value->check_source(cp);
      m_value = value;
    }
    
    template<typename V>
    void GlobalVariable::visit(V& v) {
      visit_base<Global>(v);
      v("value", &GlobalVariable::m_value);
    }
    
    PSI_TVM_VALUE_IMPL(GlobalVariable, Global);

    /**
     * \brief Get an existing module member.
     * 
     * \return NULL if no member with this name is present.
     */
    ValuePtr<Global> Module::get_member(const std::string& name) {
      ModuleMemberList::const_iterator ii = m_members.find(name);
      return (ii != m_members.end()) ? ii->second : ValuePtr<Global>();
    }
    
    /**
     * \brief Create a new global.
     * 
     * This creates either a function or a global variable based on what \c type is.
     */
    ValuePtr<Global> Module::new_member(const std::string& name, const ValuePtr<>& type, const SourceLocation& location) {
      if (ValuePtr<FunctionType> ftype = dyn_cast<FunctionType>(type))
        return new_function(name, ftype, location);
      else
        return new_global_variable(name, type, location);
    }
    
    /**
     * \brief Create a new global term.
     */
    ValuePtr<GlobalVariable> Module::new_global_variable(const std::string& name, const ValuePtr<>& type, const SourceLocation& location) {
      PSI_FAIL("Check global variable type source");
      ValuePtr<GlobalVariable> result(::new GlobalVariable(context(), type, name, this, location));
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
        m_hash_value_set(HashTermSetType::bucket_traits(m_hash_term_buckets.get(), initial_hash_term_buckets)) {
    }

    struct Context::ValueDisposer {
      void operator () (Value *t) const {
        PSI_WARNING(t->m_reference_count == 1);
        intrusive_ptr_release(t);
      }
    };

    Context::~Context() {
      // Increment refcount to ensure nothing is destroyed yet
      for (TermListType::iterator ii = m_value_list.begin(), ie = m_value_list.end(); ii != ie; ++ii)
        ++ii->m_reference_count;

      // Release all internal references
      for (TermListType::iterator ii = m_value_list.begin(), ie = m_value_list.end(); ii != ie; ++ii)
        ii->gc_clear();
        
      m_value_list.clear_and_dispose(ValueDisposer());

      PSI_WARNING(m_hash_value_set.empty());
    }
    
    namespace {
      struct HashableEqualsData {
        std::size_t hash;
        const char *operation;
        const HashableValue *value;
      };

      struct HashableSetupHasher {
        std::size_t operator () (const HashableEqualsData& arg) const {
          return arg.hash;
        }
      };
    }
    
    struct Context::HashableSetupEquals {
      bool operator () (const HashableEqualsData& lhs, const HashableValue& rhs) const {
        if (lhs.hash != rhs.m_hash)
          return false;
        if (lhs.operation != rhs.m_operation)
          return false;
        return lhs.value->equals_impl(rhs);
      }
    };

    /**
     * \brief Get an existing hashable term, or create a new one.
     */
    ValuePtr<HashableValue> Context::get_hash_term(const HashableValue& value) {
      std::pair<const char*, std::size_t> hash = value.hash_impl();
      HashableEqualsData data;
      data.hash = hash.second;
      data.operation = hash.first;
      data.value = &value;

      HashTermSetType::insert_commit_data commit_data;
      std::pair<HashTermSetType::iterator, bool> r = m_hash_value_set.insert_check(data, HashableSetupHasher(), HashableSetupEquals(), commit_data);
      if (!r.second)
        return ValuePtr<HashableValue>(&*r.first);

      ValuePtr<HashableValue> result(value.clone());
      result->set_type(result->check_type());
      result->m_operation = hash.first;
      result->m_hash = hash.second;
      m_hash_value_set.insert_commit(*result, commit_data);
      return result;
    }

#ifdef PSI_DEBUG
    /**
     * Dump the contents of the hash_terms table to stderr.
     */
    void Context::dump_hash_terms() {
      for (HashTermSetType::iterator it = m_hash_value_set.begin(); it != m_hash_value_set.end(); ++it) {
        std::cerr << &*it << ": " << it->m_hash << "\n";
        it->dump();
        std::cerr << '\n';
      }
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
    m_name(name) {
    }
    
    Module::~Module() {
    }

    void Module::add_member(const ValuePtr<Global>& term) {
      if (!m_members.insert(std::make_pair(term->name(), term)).second)
        throw TvmUserError("Duplicate module member name");
    }
    
#ifdef PSI_DEBUG
    /**
     * Dump all symbols in this module to stderr.
     */
    void Module::dump() {
      print_module(std::cerr, this);
    }
#endif
    
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
