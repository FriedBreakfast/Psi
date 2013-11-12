#include "Aggregate.hpp"
#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Recursive.hpp"
#include "Utility.hpp"

#if PSI_DEBUG
#include <iostream>
#endif

namespace Psi {
  namespace Tvm {
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
          PSI_ASSERT((term_type == term_functional) || (term_type == term_apply) || (term_type == term_function_type)
            || (term_type == term_exists) || (term_type == term_upref_null) || (term_type == term_resolved_parameter));
          m_category = category_undetermined;
        }
      } else {
        if (m_context != type->m_context)
          context.error_context().error_throw(location, "context mismatch between term and its type");

        PSI_ASSERT(type->m_category != category_undetermined);
        switch (type->m_category) {
        case category_metatype: m_category = category_type; break;
        case category_type: m_category = category_value; break;

        default:
          context.error_context().error_throw(location, "type of a term cannot be a value or recursive, it must be metatype or a type", CompileError::error_internal);
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
    
#if PSI_DEBUG
    /**
     * Dump this term to stderr.
     */
    void Value::dump() {
      print_term(std::cerr, ValuePtr<>(this));
    }
#endif

    /**
     * \brief Checks whether a value of type child can be used in place of a value of type parent.
     */
    bool Value::match(const ValuePtr<>& child) const {
      std::vector<ValuePtr<> > wildcards;
      return match(child, wildcards);
    }

    /**
     * \brief Checks whether another tree matches this one, which is a pattern.
     * 
     * \param upref_write Whether NULL upward references should be considered from the point of view
     * of reading or writing. If false, a shorter chain in this object is considered to match a longer
     * chain in \c child, if true the reverse holds.
     */
    bool Value::match(const ValuePtr<>& child, std::vector<ValuePtr<> >& wildcards, unsigned depth, UprefMatchMode upref_mode) const {
      if (term_type() == term_resolved_parameter) {
        const ResolvedParameter& rp = checked_cast<const ResolvedParameter&>(*this);
        if (rp.depth() == depth) {
          // Check type also matches
          if (!rp.type()->match(child->type(), wildcards, depth, upref_match_exact))
            return false;

          if (rp.index() >= wildcards.size())
            return false;

          ValuePtr<>& wildcard = wildcards[rp.index()];
          if (wildcard) {
            std::vector<ValuePtr<> > empty_wildcards;
            // Need to do this rather than == because upref_match_exact does not always imply equivalence
            return wildcard->match(child, empty_wildcards, 0, upref_match_exact);
          } else {
            wildcard = child;
            return true;
          }
        }
      }
      
      if (this == child.get())
        return true;
      
      // Note case of both being UpwardReferenceNull is handled by checking for reference equality
      if (term_type() == term_upref_null)
        return upref_mode == upref_match_read;
      else if (child->term_type() == term_upref_null)
        return upref_mode == upref_match_write;
      
      if (term_type() != child->term_type())
        return false;
      
      switch (term_type()) {
      case term_functional: {
        const FunctionalValue& this_fn = checked_cast<const FunctionalValue&>(*this);
        const FunctionalValue& child_fn = checked_cast<const FunctionalValue&>(*child);
        if (this_fn.operation_name() != child_fn.operation_name())
          return false;
        return this_fn.match_impl(child_fn, wildcards, depth, upref_mode);
      }
      
      case term_apply: {
        const ApplyType& this_ap = checked_cast<const ApplyType&>(*this);
        const ApplyType& child_ap = checked_cast<const ApplyType&>(*child);
        if (this_ap.recursive() != child_ap.recursive())
          return false;
        PSI_ASSERT(this_ap.parameters().size() == child_ap.parameters().size());
        for (std::size_t ii = 0, ie = this_ap.parameters().size(); ii != ie; ++ii) {
          if (!this_ap.parameters()[ii]->match(child_ap.parameters()[ii], wildcards, depth, upref_mode))
            return false;
        }
        return true;
      }

      case term_function_type: {
        const FunctionType& this_ft = checked_cast<const FunctionType&>(*this);
        const FunctionType& child_ft = checked_cast<const FunctionType&>(*child);
        if ((this_ft.parameter_types().size() != child_ft.parameter_types().size()) ||
            (this_ft.n_phantom() != child_ft.n_phantom()) ||
            (this_ft.sret() != child_ft.sret()) ||
            (this_ft.calling_convention() != child_ft.calling_convention())) {
          return false;
        }
        
        for (std::size_t ii = 0, ie = this_ft.parameter_types().size(); ii != ie; ++ii) {
          const ParameterType& this_param_ty = this_ft.parameter_types()[ii];
          const ParameterType& child_param_ty = child_ft.parameter_types()[ii];
          if (this_param_ty.attributes != child_param_ty.attributes)
            return false;
          if (!this_param_ty.value->match(child_param_ty.value, wildcards, depth+1, upref_mode))
            return false;
        }
        
        UprefMatchMode reverse_mode;
        switch (upref_mode) {
        case upref_match_read: reverse_mode = upref_match_write;
        case upref_match_write: reverse_mode = upref_match_read;
        case upref_match_exact: reverse_mode = upref_match_exact;
        default: PSI_FAIL("Unrecognised enumeration value");
        }
        
        if (this_ft.result_type().attributes != child_ft.result_type().attributes)
          return false;
        if (!this_ft.result_type().value->match(child_ft.result_type().value, wildcards, depth+1, reverse_mode))
          return false;
        
        return true;
      }
      
      case term_exists: {
        const Exists& this_ex = checked_cast<const Exists&>(*this);
        const Exists& child_ex = checked_cast<const Exists&>(*child);
        if (this_ex.parameter_types().size() != child_ex.parameter_types().size())
          return false;
        
        for (std::size_t ii = 0, ie = this_ex.parameter_types().size(); ii != ie; ++ii) {
          if (!this_ex.parameter_types()[ii]->match(child_ex.parameter_types()[ii], wildcards, depth+1, upref_match_exact))
            return false;
        }
        
        return this_ex.result()->match(child_ex.result(), wildcards, depth+1, upref_mode);
      }
      
      default:
        // All other cases cannot be base types
        return false;
      }
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
      m_module(module),
      m_linkage(link_private) {
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
        error_context().error_throw(location(), "Cannot mix global variables between modules");
    }

    GlobalVariable::GlobalVariable(Context& context, const ValuePtr<>& type, const std::string& name, Module *module, const SourceLocation& location)
    : Global(context, term_global_variable, type, name, module, location),
      m_constant(false),
      m_merge(false) {
    }

    void GlobalVariable::set_value(const ValuePtr<>& value) {
      if (value->type() != value_type())
        error_context().error_throw(location(), "Global variable assigned value of incorrect type");
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
      ValuePtr<GlobalVariable> result(::new GlobalVariable(context(), type, name, this, location));
      CheckSourceParameter cp(CheckSourceParameter::mode_global, result.get());
      type->check_source(cp);
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

    Context::Context(CompileErrorContext *error_context)
      : m_error_context(error_context),
        m_hash_term_buckets(initial_hash_term_buckets),
        m_hash_value_set(HashTermSetType::bucket_traits(m_hash_term_buckets.get(), m_hash_term_buckets.size())) {
    }

    struct Context::ValueDisposer {
      void operator () (Value *t) const {
        PSI_WARNING_MSG(t->m_reference_count == 1, typeid(t).name());
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
      
      if (m_hash_value_set.size() >= m_hash_value_set.bucket_count()) {
        UniqueArray<HashTermSetType::bucket_type> new_buckets(m_hash_value_set.bucket_count() * 2);
        m_hash_value_set.rehash(HashTermSetType::bucket_traits(new_buckets.get(), new_buckets.size()));
        swap(new_buckets, m_hash_term_buckets);
      }
      
      return result;
    }

#if PSI_DEBUG
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
        context().error_context().error_throw(location(), "Duplicate module member name");
    }
    
#if PSI_DEBUG
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
    
    /**
     * \brief Combine two sets of parameter attributes.
     * 
     * Currently this doesn't do any sort of error checking that the result is consistent.
     */
    ParameterAttributes combine_attributes(const ParameterAttributes& lhs, const ParameterAttributes& rhs) {
      ParameterAttributes result;
      result.flags = lhs.flags | rhs.flags;
      return result;
    }
    
    /**
     * \brief Get a string representation of a calling convention, for error message purposes.
     */
    const char* cconv_name(CallingConvention cc) {
      switch (cc) {
      case cconv_c: return "c";
      case cconv_x86_stdcall: return "x86_stdcall";
      case cconv_x86_thiscall: return "x86_thiscall";
      case cconv_x86_fastcall: return "x86_fastcall";
      default: return "??";
      }
    }
  }
}
