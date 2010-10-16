#include "Core.hpp"

#include <stdexcept>
#include <typeinfo>

#include <boost/smart_ptr/scoped_array.hpp>
#include <boost/type_traits/alignment_of.hpp>

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

    TermPtrBase::TermPtrBase()
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
    }

    TermPtrBase::TermPtrBase(const TermPtrBase& src)
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
      reset(src.get());
    }

    TermPtrBase::TermPtrBase(Term *ptr)
      : TermUser(UserInitializer(1, m_uses), term_ptr) {
      reset(ptr);
    }

    TermPtrBase::~TermPtrBase() {
      reset(0);
    }

    void TermPtrBase::reset(Term *term) {
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

    Term::Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, Term *type)
      : TermUser(ui, term_type), m_abstract(abstract), m_parameterized(parameterized), m_global(global), m_use_count_ptr(0), m_context(context) {

      m_use_count.value = 0;

      if (!type) {
	m_category = category_metatype;
	PSI_ASSERT_MSG((term_type == term_metatype) && !abstract, "term with no type is not a valid metatype");
      } else {
	PSI_ASSERT_MSG(context == type->m_context, "context mismatch between term and its type");
	if (type->m_category == category_metatype) {
	  m_category = category_type;
	} else {
	  PSI_ASSERT_MSG(type->m_category == category_type, "term does has invalid category");
	  m_category = category_value;
	}
      }

      use_set(0, type);
    }

    namespace {
      /*
       * These are templates because the pointer-to-pointer parameter
       * prevents using a parent type.
       */

      template<typename T>
      bool any_abstract(std::size_t n, T *const* t) {
	return std::find_if(t, t+n, std::mem_fun(&Term::abstract)) != (t+n);
      }

      template<typename T>
      bool any_parameterized(std::size_t n, T *const* t) {
	return std::find_if(t, t+n, std::mem_fun(&Term::parameterized)) != (t+n);
      }

      template<typename T>
      bool all_global(std::size_t n, T *const* t) {
	return std::find_if(t, t+n, std::not1(std::mem_fun(&Term::global))) != (t+n);
      }
    }

    std::size_t Term::hash_value() const {
      switch (term_type()) {
      case term_functional:
      case term_function_type_internal:
      case term_function_type_internal_parameter:
	return boost::polymorphic_downcast<const HashTerm*>(this)->m_hash;

      default:
	PSI_ASSERT(!dynamic_cast<const HashTerm*>(this));
	return boost::hash_value(this);
      }
    }

    std::size_t* Term::term_use_count() {
      if (m_use_count_ptr)
	return m_use_count.ptr;
      else
	return &m_use_count.value;
    }

    void Term::term_add_ref() {
      ++*term_use_count();
    }

    void Term::term_release() {
      if (!--*term_use_count())
	term_destroy(this);
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
	  if (visited.insert(&*it).second)
	    queue.push_back(&*it);
	}

	std::size_t n_uses = current->n_uses();
	for (std::size_t i = 0; i < n_uses; ++i) {
	  Term *child = current->use_get(i);
	  std::size_t *child_use_count = child->term_use_count();
	  if (!*child_use_count || !--*child_use_count) {
	    if (visited.insert(child).second)
	      queue.push_back(child);
	  }

	  current->use_set(i, 0);
	}

	current->clear_users();
	delete current;
      }
    }

    namespace {
      inline std::size_t struct_offset(std::size_t base, std::size_t size, std::size_t align) {
	return (base + size + align - 1) & ~align;
      }

      inline void* ptr_offset(void *p, std::size_t offset) {
	return static_cast<void*>(static_cast<char*>(p) + offset);
      }

      template<typename T>
      struct InitializerBase {
	typedef T TermType;

	std::size_t term_size() const {
	  return sizeof(T);
	}
      };
    }

    template<typename T>
    typename T::TermType* Context::allocate_term(const T& initializer) {
      std::size_t n_uses = initializer.n_uses();

      std::size_t use_offset = struct_offset(0, initializer.term_size(), boost::alignment_of<Use>::value);
      std::size_t total_size = use_offset + sizeof(Use)*(n_uses+2);

      void *term_base = operator new (total_size);
      Use *uses = static_cast<Use*>(ptr_offset(term_base, use_offset));
      try {
	return initializer.initialize(term_base, UserInitializer(n_uses+1, uses), this);
      } catch(...) {
	operator delete (term_base);
	throw;
      }
    }

    namespace {
      template<typename T>
      struct SetupHasher {
	std::size_t operator () (const T& key) const {
	  return key.hash();
	}
      };

      template<typename T>
      struct SetupEquals {
	std::size_t operator () (const T& key, const HashTerm& value) const {
	  return key.equals(&value);
	}
      };
    }

    template<typename T>
    typename T::TermType* Context::hash_term_get(T& setup) {
      typename HashTermSetType::insert_commit_data commit_data;
      std::pair<typename HashTermSetType::iterator, bool> existing =
	m_hash_terms.insert_check(setup, SetupHasher<T>(), SetupEquals<T>(), commit_data);
      if (!existing.second)
	return boost::polymorphic_downcast<typename T::TermType*>(&*existing.first);

      setup.prepare_initialize(this);
      typename T::TermType *term = allocate_term(setup);
      m_hash_terms.insert_commit(*term, commit_data);

      if (m_hash_terms.size() >= m_hash_terms.bucket_count()) {
	std::size_t n_buckets = m_hash_terms.bucket_count() * 2;
	UniqueArray<typename HashTermSetType::bucket_type> buckets
	  (new typename HashTermSetType::bucket_type[n_buckets]);
	m_hash_terms.rehash(typename HashTermSetType::bucket_traits(buckets.get(), n_buckets));
	m_hash_term_buckets.swap(buckets);
      }

      return term;
    }

    HashTerm::HashTerm(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, bool global, Term *type, std::size_t hash)
      : Term(ui, context, term_type, abstract, parameterized, global, type),
	m_hash(hash) {
    }

    MetatypeTerm::MetatypeTerm(const UserInitializer& ui, Context* context)
      : Term(ui, context, term_metatype, false, false, true, NULL) {
    }

    class MetatypeTerm::Initializer : public InitializerBase<MetatypeTerm> {
    public:
      std::size_t n_uses() const {
	return 0;
      }

      MetatypeTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) MetatypeTerm(ui, context);
      }
    };

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

    FunctionalTerm::FunctionalTerm(const UserInitializer& ui, Context *context, Term *type,
				   std::size_t hash, FunctionalTermBackend *backend,
				   std::size_t n_parameters, Term *const* parameters)
      : HashTerm(ui, context, term_functional,
		 type->abstract() || any_abstract(n_parameters, parameters),
		 type->parameterized() || any_parameterized(n_parameters, parameters),
		 type->global() && all_global(n_parameters, parameters),
		 type, hash),
	m_backend(backend) {
      for (std::size_t i = 0; i < n_parameters; ++i)
	set_base_parameter(i, parameters[i]);
    }

    FunctionalTerm::~FunctionalTerm() {
      m_backend->~FunctionalTermBackend();
    }

    class FunctionalTerm::Setup {
    public:
      typedef FunctionalTerm TermType;

      Setup(std::size_t n_parameters, Term *const* parameters, const FunctionalTermBackend *backend)
	: m_n_parameters(n_parameters),
	  m_parameters(parameters),
	  m_backend(backend) {

	m_hash = 0;
	boost::hash_combine(m_hash, backend->hash_value());
	for (std::size_t i = 0; i < n_parameters; ++i)
	  boost::hash_combine(m_hash, parameters[i]->hash_value());
      }

      void prepare_initialize(Context *context) {
	m_type = m_backend->type(*context, m_n_parameters, m_parameters);

	std::pair<std::size_t, std::size_t> backend_size_align = m_backend->size_align();
	PSI_ASSERT_MSG((backend_size_align.second & (backend_size_align.second - 1)) == 0, "alignment is not a power of two");
	m_proto_offset = struct_offset(0, sizeof(FunctionalTerm), backend_size_align.second);
	m_size = m_proto_offset + backend_size_align.first;
      }

      FunctionalTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	FunctionalTermBackend *new_backend = m_backend->clone(ptr_offset(base, m_proto_offset));
	try {
	  return new (base) FunctionalTerm(ui, context, m_type, m_hash, new_backend, m_n_parameters, m_parameters);
	} catch(...) {
	  new_backend->~FunctionalTermBackend();
	  throw;
	}
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t term_size() const {
	return m_size;
      }

      std::size_t n_uses() const {
	return m_n_parameters;
      }

      bool equals(const HashTerm *term) const {
	if ((m_hash != term->m_hash) || (term->term_type() != term_functional))
	  return false;

	const FunctionalTerm *cast_term = boost::polymorphic_downcast<const FunctionalTerm*>(term);

	if (m_n_parameters != cast_term->n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_n_parameters; ++i) {
	  if (m_parameters[i] != cast_term->parameter(i))
	    return false;
	}

	if ((typeid(*m_backend) != typeid(*cast_term->m_backend))
	    || !m_backend->equals(*cast_term->m_backend))
	  return false;

	return true;
      }

    private:
      std::size_t m_proto_offset;
      std::size_t m_size;
      std::size_t m_hash;
      std::size_t m_n_parameters;
      Term *m_type;
      Term *const* m_parameters;
      const FunctionalTermBackend *m_backend;
    };

    FunctionalTerm* Context::get_functional_internal(const FunctionalTermBackend& backend, std::size_t n_parameters, Term *const* parameters) {
      FunctionalTerm::Setup setup(n_parameters, parameters, &backend);
      return hash_term_get(setup);
    }

    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui, Context *context,
				       Term *result_type, std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters)
      : Term(ui, context, term_function_type,
	     result_type->abstract() || any_abstract(n_parameters, parameters), true,
	     result_type->global() && all_global(n_parameters, parameters),
	     context->get_metatype().get()) {
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	set_base_parameter(i+1, parameters[i]);
      }
    }

    class FunctionTypeTerm::Initializer : public InitializerBase<FunctionTypeTerm> {
    public:
      Initializer(Term *result_type, std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters)
	: m_result_type(result_type), m_n_parameters(n_parameters), m_parameters(parameters) {
      }

      std::size_t n_uses() const {
	return m_n_parameters + 1;
      }

      FunctionTypeTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeTerm(ui, context, m_result_type, m_n_parameters, m_parameters);
      }

    private:
      Term *m_result_type;
      std::size_t m_n_parameters;
      FunctionTypeParameterTerm *const* m_parameters;
    };

    /**
     * Check whether part of function type term is complete,
     * i.e. whether there are still function parameters which have
     * to be resolved by further function types (this happens in the
     * case of nested function types).
     */
    bool Context::check_function_type_complete(Term *term, std::tr1::unordered_set<FunctionTypeTerm*>& functions)
    {
      if (!term->parameterized())
	return true;

      if (!check_function_type_complete(term->type(), functions))
	return false;

      switch(term->term_type()) {
      case term_functional: {
	FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->parameter(i), functions))
	    return false;
	}
	return true;
      }

      case term_function_type: {
	FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	functions.insert(cast_term);
	if (!check_function_type_complete(cast_term->result_type(), functions))
	  return false;
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->parameter(i)->type(), functions))
	    return false;
	}
	functions.erase(cast_term);
	return true;
      }

      case term_function_type_parameter: {
	FunctionTypeParameterTerm *cast_term = static_cast<FunctionTypeParameterTerm*>(term);
	FunctionTypeTerm *source = cast_term->source();
	if (!source)
	  return false;

	if (functions.find(source) != functions.end())
	  throw std::logic_error("type of function parameter appeared outside of function type definition");

	return true;
      }

      default:
	// all terms should either be amongst the handled cases or complete
	PSI_FAIL("unknown term type");
      }
    }

    Term* Context::build_function_type_resolver_term(std::size_t depth, Term *term, FunctionResolveMap& functions) {
      if (!term->parameterized())
	return term;

      switch(term->term_type()) {
      case term_functional: {
	FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	Term *type = build_function_type_resolver_term(depth, cast_term->type(), functions);
	std::size_t n_parameters = cast_term->n_parameters();
	boost::scoped_array<Term*> parameters(new Term*[n_parameters]);
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++)
	  parameters[i] = build_function_type_resolver_term(depth, term, functions);
	return get_functional_internal_with_type(*cast_term->m_backend, type, n_parameters, parameters.get());
      }

      case term_function_type: {
	FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	PSI_ASSERT(functions.find(cast_term) == functions.end());
	FunctionResolveStatus& status = functions[cast_term];
	status.depth = depth + 1;
	status.index = 0;

	std::size_t n_parameters = cast_term->n_parameters();
	boost::scoped_array<Term*> parameter_types(new Term*[n_parameters]);
	for (std::size_t i = 0; i < n_parameters; ++i) {
	  parameter_types[i] = build_function_type_resolver_term(depth+1, cast_term->parameter(i)->type(), functions);
	  status.index++;
	}

	Term *result_type = build_function_type_resolver_term(depth+1, cast_term->result_type(), functions);
	functions.erase(cast_term);

	return get_function_type_internal(result_type, n_parameters, parameter_types.get());
      }

      case term_function_type_parameter: {
	FunctionTypeParameterTerm *cast_term = static_cast<FunctionTypeParameterTerm*>(term);
	FunctionTypeTerm *source = cast_term->source();

	FunctionResolveMap::iterator it = functions.find(source);
	PSI_ASSERT(it != functions.end());

	if (cast_term->index() >= it->second.index)
	  throw std::logic_error("function type parameter definition refers to value of later parameter");

	return get_function_type_internal_parameter(depth - it->second.depth, cast_term->index());
      }

      default:
	// all terms should either be amongst the handled cases or complete
	PSI_FAIL("unknown term type");
      }
    }

    TermPtr<FunctionTypeTerm> Context::get_function_type(Term *result_type, std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters) {
      for (std::size_t i = 0; i < n_parameters; ++i)
	PSI_ASSERT(!parameters[i]->source());

      TermPtr<FunctionTypeTerm> term(allocate_term(FunctionTypeTerm::Initializer(result_type, n_parameters, parameters)));

      for (std::size_t i = 0; i < n_parameters; ++i) {
	parameters[i]->m_index = i;
	parameters[i]->set_source(term.get());
      }

      // it's only possible to merge complete types, since incomplete
      // types depend on higher up terms which have not yet been
      // built.
      std::tr1::unordered_set<FunctionTypeTerm*> check_functions;
      if (!check_function_type_complete(term.get(), check_functions))
	return term;

      term->m_parameterized = false;

      FunctionResolveMap functions;
      FunctionResolveStatus& status = functions[term.get()];
      status.depth = 0;
      status.index = 0;

      boost::scoped_array<Term*> internal_parameter_types(new Term*[n_parameters]);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	internal_parameter_types[i] = build_function_type_resolver_term(0, parameters[i]->type(), functions);
	status.index++;
      }

      Term *internal_result_type = build_function_type_resolver_term(0, term->result_type(), functions);
      PSI_ASSERT((functions.erase(term.get()), functions.empty()));

      FunctionTypeInternalTerm *internal = get_function_type_internal(internal_result_type, n_parameters, internal_parameter_types.get());
      if (internal->get_function_type()) {
	// A matching type exists
	return TermPtr<FunctionTypeTerm>(internal->get_function_type());
      } else {
	internal->set_function_type(term.get());
	return term;
      }
    }

    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term *type)
      : Term(ui, context, term_function_type_parameter, type->abstract(), true, type->global(), type),
	m_index(0) {
    }

    class FunctionTypeParameterTerm::Initializer : public InitializerBase<FunctionTypeParameterTerm> {
    public:
      Initializer(Term *type) : m_type(type) {}

      std::size_t n_uses() const {
	return 1;
      }

      FunctionTypeParameterTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeParameterTerm(ui, context, m_type);
      }

    private:
      Term *m_type;
    };

    TermPtr<FunctionTypeParameterTerm> Context::new_function_type_parameter(Term *type) {
      return TermPtr<FunctionTypeParameterTerm>(allocate_term(FunctionTypeParameterTerm::Initializer(type)));
    }

    FunctionTypeInternalTerm::FunctionTypeInternalTerm(const UserInitializer& ui, Context *context, std::size_t hash, Term *result_type, std::size_t n_parameters, Term *const* parameter_types)
      : HashTerm(ui, context, term_function_type_internal,
		 result_type->abstract() || any_abstract(n_parameters, parameter_types), true,
		 result_type->global() && all_global(n_parameters, parameter_types),
		 context->get_metatype().get(), hash) {
      set_base_parameter(1, result_type);
      for (std::size_t i = 0; i < n_parameters; i++)
	set_base_parameter(i+2, parameter_types[i]);
    }

    class FunctionTypeInternalTerm::Setup : public InitializerBase<FunctionTypeInternalTerm> {
    public:
      Setup(Term *result_type, std::size_t n_parameters, Term *const* parameter_types)
	: m_n_parameters(n_parameters),
	  m_parameter_types(parameter_types),
	  m_result_type(result_type) {
	m_hash = 0;
	boost::hash_combine(m_hash, result_type->hash_value());
	for (std::size_t i = 0; i < n_parameters; ++i)
	  boost::hash_combine(m_hash, parameter_types[i]->hash_value());
      }

      void prepare_initialize(Context*) {
      }

      FunctionTypeInternalTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionTypeInternalTerm(ui, context, m_hash, m_result_type, m_n_parameters, m_parameter_types);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t n_uses() const {
	return m_n_parameters + 2;
      }

      bool equals(const HashTerm *term) const {
	if ((m_hash != term->m_hash) || (term->term_type() != term_function_type_internal))
	  return false;

	const FunctionTypeInternalTerm *cast_term =
	  boost::polymorphic_downcast<const FunctionTypeInternalTerm*>(term);

	if (m_n_parameters != cast_term->n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_n_parameters; ++i) {
	  if (m_parameter_types[i] != cast_term->parameter_type(i))
	    return false;
	}

	if (m_result_type != cast_term->result_type())
	  return false;

	return true;
      }

    private:
      std::size_t m_hash;
      std::size_t m_n_parameters;
      Term *const* m_parameter_types;
      Term *m_result_type;
    };

    FunctionTypeInternalTerm* Context::get_function_type_internal(Term *result, std::size_t n_parameters, Term *const* parameter_types) {
      FunctionTypeInternalTerm::Setup setup(result, n_parameters, parameter_types);
      return hash_term_get(setup);
    }

    class FunctionTypeInternalParameterTerm::Setup
      : public InitializerBase<FunctionTypeInternalParameterTerm> {
    public:
      Setup(std::size_t depth, std::size_t index)
	: m_depth(depth), m_index(index) {

	m_hash = 0;
	boost::hash_combine(m_hash, depth);
	boost::hash_combine(m_hash, m_index);
      }

      void prepare_initialize(Context*) {
      }

      FunctionTypeInternalParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionTypeInternalParameterTerm(ui, context, m_hash, m_depth, m_index);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t n_uses() const {
	return 0;
      }

      bool equals(const HashTerm *term) const {
	if ((m_hash != term->m_hash) || (term->term_type() != term_function_type_internal_parameter))
	  return false;

	const FunctionTypeInternalParameterTerm *cast_term =
	  boost::polymorphic_downcast<const FunctionTypeInternalParameterTerm*>(term);

	if (m_depth != cast_term->m_depth)
	  return false;

	if (m_index != cast_term->m_index)
	  return false;

	return true;
      }

    private:
      std::size_t m_depth;
      std::size_t m_index;
      std::size_t m_hash;
    };

    FunctionTypeInternalParameterTerm* Context::get_function_type_internal_parameter(std::size_t depth, std::size_t index) {
      FunctionTypeInternalParameterTerm::Setup setup(depth, index);
      return hash_term_get(setup);
    }

    RecursiveParameterTerm::RecursiveParameterTerm(const UserInitializer& ui, Context *context, Term *type)
      : Term(ui, context, term_recursive_parameter, true, false, type->global(), type) {
    }

    class RecursiveParameterTerm::Initializer : public InitializerBase<RecursiveParameterTerm> {
    public:
      Initializer(Term *type) : m_type(type) {}

      RecursiveParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) RecursiveParameterTerm(ui, context, m_type);
      }

      std::size_t n_uses() const {return 0;}

    private:
      Term *m_type;
    };

    RecursiveParameterTerm* Context::new_recursive_parameter(Term *type) {
      return allocate_term(RecursiveParameterTerm::Initializer(type));
    }

    RecursiveTerm::RecursiveTerm(const UserInitializer& ui, Context *context, Term *result_type,
				 bool global, std::size_t n_parameters, RecursiveParameterTerm *const* parameters)
      : Term(ui, context, term_recursive, true, false, global, context->get_metatype().get()) {
      PSI_ASSERT(!global || (result_type->global() && all_global(n_parameters, parameters)));
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	set_base_parameter(i+2, parameters[i]);
      }
    }

    class RecursiveTerm::Initializer : public InitializerBase<RecursiveTerm> {
    public:
      Initializer(bool global, Term *type, std::size_t n_parameters, RecursiveParameterTerm *const* parameters)
	: m_global(global), m_type(type), m_n_parameters(n_parameters), m_parameters(parameters) {
      }

      RecursiveTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) RecursiveTerm(ui, context, m_type, m_global, m_n_parameters, m_parameters);
      }

      std::size_t n_uses() const {return 1;}

    private:
      bool m_global;
      Term *m_type;
      std::size_t m_n_parameters;
      RecursiveParameterTerm *const* m_parameters;
    };

    /**
     * \brief Create a new recursive term.
     */
    TermPtr<RecursiveTerm> Context::new_recursive(bool global, Term *result_type,
						  std::size_t n_parameters,
						  Term *const* parameter_types) {
      boost::scoped_array<RecursiveParameterTerm*> parameters(new RecursiveParameterTerm*[n_parameters]);
      for (std::size_t i = 0; i < n_parameters; ++i)
	parameters[i] = new_recursive_parameter(parameter_types[i]);
      return TermPtr<RecursiveTerm>(allocate_term(RecursiveTerm::Initializer(global, result_type, n_parameters, parameters.get())));
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveTerm::resolve(Term *term) {
      return context().resolve_recursive(this, term);
    }

    TermPtr<ApplyTerm> RecursiveTerm::apply(std::size_t n_parameters, Term *const* parameters) {
      return TermPtr<ApplyTerm>(context().apply_recursive(this, n_parameters, parameters));
    }

    ApplyTerm::ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
			 std::size_t n_parameters, Term *const* parameters)
      : Term(ui, context, term_apply,
	     recursive->abstract() || any_abstract(n_parameters, parameters), false,
	     recursive->global() && all_global(n_parameters, parameters),
	     context->get_metatype().get()) {
      set_base_parameter(0, recursive);
      for (std::size_t i = 0; i < n_parameters; ++i)
	set_base_parameter(i+1, parameters[i]);
    }

    class ApplyTerm::Initializer : public InitializerBase<ApplyTerm> {
    public:
      Initializer(RecursiveTerm *recursive, std::size_t n_parameters, Term *const* parameters)
	: m_recursive(recursive), m_n_parameters(n_parameters), m_parameters(parameters) {
      }

      ApplyTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) ApplyTerm(ui, context, m_recursive, m_n_parameters, m_parameters);
      }

      std::size_t n_uses() const {return m_n_parameters + 1;}

    private:
      RecursiveTerm *m_recursive;
      std::size_t m_n_parameters;
      Term *const* m_parameters;
    };

    TermPtr<ApplyTerm> Context::apply_recursive(RecursiveTerm *recursive,
						std::size_t n_parameters,
						Term *const* parameters) {
      return TermPtr<ApplyTerm>(allocate_term(ApplyTerm::Initializer(recursive, n_parameters, parameters)));
    }

    TermPtr<> ApplyTerm::unpack() const {
    }

    GlobalTerm::GlobalTerm(const UserInitializer& ui, Context *context, TermType term_type, Term *type)
      : Term(ui, context, term_type, false, false, true, type) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
    }

    GlobalVariableTerm::GlobalVariableTerm(const UserInitializer& ui, Context *context, Term *type, bool constant)
      : GlobalTerm(ui, context, term_global_variable, type),
	m_constant(constant) {
    }

    void GlobalVariableTerm::set_value(Term *value) {
      if (!value->global())
	throw std::logic_error("value of global variable must be a global");

      set_base_parameter(0, value);
    }

    class GlobalVariableTerm::Initializer : public InitializerBase<GlobalVariableTerm> {
    public:
      Initializer(Term *type, bool constant)
	: m_type(type), m_constant(constant) {
      }

      GlobalVariableTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) GlobalVariableTerm(ui, context, m_type, m_constant);
      }

      std::size_t n_uses() const {
	return 1;
      }

    private:
      Term *m_type;
      bool m_constant;
    };

    /**
     * \brief Create a new global term.
     */
    TermPtr<GlobalVariableTerm> Context::new_global_variable(Term *type, bool constant) {
      return TermPtr<GlobalVariableTerm>(allocate_term(GlobalVariableTerm::Initializer(type, constant)));
    }

    FunctionParameterTerm::FunctionParameterTerm(const UserInitializer& ui, Context *context, Term *type)
      : Term(ui, context, term_function_parameter, false, false, type->global(), type) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
    }

    class FunctionParameterTerm::Initializer : public InitializerBase<FunctionParameterTerm> {
    public:
      Initializer(Term *type) : m_type(type) {
      }

      std::size_t n_uses() const {
	return 0;
      }

      FunctionParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionParameterTerm(ui, context, m_type);
      }

    private:
      Term *m_type;
    };

    namespace {
      void insert_if_abstract(std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set, Term *term) {
	if (term->abstract()) {
	  if (set.insert(term).second)
	    queue.push_back(term);
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
	  FunctionalTerm *cast_term = boost::polymorphic_downcast<FunctionalTerm*>(term);
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    insert_if_abstract(queue, set, cast_term->parameter(i));
	  break;
	}

	case term_recursive: {
	  RecursiveTerm *cast_term = boost::polymorphic_downcast<RecursiveTerm*>(term);
	  if (!cast_term->result()) {
	    queue.clear();
	    set.clear();
	    return true;
	  }
	  insert_if_abstract(queue, set, cast_term->result());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); i++)
	    insert_if_abstract(queue, set, cast_term->parameter(i)->type());
	  break;
	}

	case term_function_type: {
	  FunctionTypeTerm *cast_term = boost::polymorphic_downcast<FunctionTypeTerm*>(term);
	  insert_if_abstract(queue, set, cast_term->result_type());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    insert_if_abstract(queue, set, cast_term->parameter(i)->type());
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

    void Context::clear_and_queue_if_abstract(std::vector<Term*>& queue, Term *t) {
      if (t->abstract()) {
	t->m_abstract = false;
	queue.push_back(t);
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
	  FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	  clear_and_queue_if_abstract(queue, cast_term->type());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term->parameter(i));
	  break;
	}

	case term_recursive: {
	  RecursiveTerm *cast_term = static_cast<RecursiveTerm*>(term);
	  PSI_ASSERT(cast_term->result());
	  clear_and_queue_if_abstract(queue, cast_term->result());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term->parameter(i)->type());
	  break;
	}

	case term_function_type: {
	  FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	  clear_and_queue_if_abstract(queue, cast_term->result_type());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term->parameter(i)->type());
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
    void Context::resolve_recursive(RecursiveTerm *recursive, Term *to) {
      if (recursive->type() != to->type())
	throw std::logic_error("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
	throw std::logic_error("cannot resolve recursive term to parameterized term");

      if (recursive->result())
	throw std::logic_error("resolving a recursive term which has already been resolved");

      recursive->set_base_parameter(1, to);

      std::vector<Term*> queue;
      std::tr1::unordered_set<Term*> set;
      if (!search_for_abstract(recursive, queue, set)) {
	recursive->m_abstract = false;

	clear_abstract(recursive, queue);

	std::vector<Term*> upward_queue;
	upward_queue.push_back(recursive);
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
    void* Context::term_jit(Term *term) {
      if ((term->m_term_type != term_global_variable) &&
	  (term->m_term_type != term_function))
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
	PSI_ASSERT_MSG(m_llvm_engine.get(), "LLVM engine creation failed - most likely neither the JIT nor interpreter have been linked in");
      } else {
	m_llvm_engine->addModule(m_llvm_module.release());
      }

      m_llvm_module.reset(new llvm::Module("", *m_llvm_context));

      return m_llvm_engine->getPointerToGlobal(global);
#endif
    }

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
	PSI_ASSERT_MSG(gv->getInitializer(), "Initializer for empty global is null");
      } else {
	gv->setInitializer(init_llvm.value());
      }
    }
#endif

    Context::Context()
      : m_metatype(allocate_term(MetatypeTerm::Initializer())),
	m_hash_term_buckets(new HashTermSetType::bucket_type[initial_hash_term_buckets]),
	m_hash_terms(HashTermSetType::bucket_traits(m_hash_term_buckets.get(), initial_hash_term_buckets)) {
    }

    Context::~Context() {
      PSI_ASSERT(m_hash_terms.empty());
    }
  }
}
