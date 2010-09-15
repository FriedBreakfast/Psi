#include "Core.hpp"
//#include "Derived.hpp"
//#include "Function.hpp"

#include <stdexcept>
#include <typeinfo>

#include <boost/smart_ptr/scoped_array.hpp>

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
    template<typename TermType>
    struct Context::TermDisposer {
      void operator () (TermType *p) const {
	delete p;
      }
    };

    template<typename TermType, std::size_t initial_buckets>
    Context::TermHashSet<TermType, initial_buckets>::TermHashSet()
      : m_buckets(new typename HashSetType::bucket_type[initial_buckets]),
	m_hash_set(typename HashSetType::bucket_traits(m_buckets.get(), initial_buckets)) {
    }

    template<typename TermType, std::size_t initial_buckets>
    Context::TermHashSet<TermType, initial_buckets>::~TermHashSet() {
      m_hash_set.clear_and_dispose(TermDisposer<TermType>());
    }

    template<typename TermType, std::size_t initial_buckets>
    template<typename Key, typename KeyHash, typename KeyValueEquals, typename KeyConstructor>
    TermType* Context::TermHashSet<TermType, initial_buckets>::get(const Key& key, const KeyHash& key_hash, const KeyValueEquals& key_value_equals, const KeyConstructor& key_constructor) {
      typename HashSetType::insert_commit_data commit_data;
      std::pair<typename HashSetType::iterator, bool> existing =
	m_hash_set.insert_check(key, key_hash, key_value_equals, commit_data);
      if (!existing.second)
	return &*existing.first;

      TermType *term = key_constructor(key);
      m_hash_set.insert_commit(*term, commit_data);

      if (m_hash_set.size() >= m_hash_set.bucket_count()) {
	std::size_t n_buckets = m_hash_set.bucket_count() * 2;
	UniqueArray<typename HashSetType::bucket_type> buckets(new typename HashSetType::bucket_type[n_buckets]);
	m_hash_set.rehash(typename HashSetType::bucket_traits(buckets.get(), n_buckets));
	m_buckets.swap(buckets);
      }

      return term;
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
	typedef T* ResultType;
	static const std::size_t size = sizeof(T);
      };
    }

    template<typename T>
    typename T::ResultType Context::allocate_term(const T& initializer) {
      std::size_t use_offset = struct_offset(0, initializer.size, align_of<Use>());
      std::size_t total_size = use_offset + sizeof(Use)*(initializer.n_slots+2);

      void *term_base = operator new (total_size);
      Use *uses = static_cast<Use*>(ptr_offset(term_base, use_offset));
      try {
	return initializer.init(term_base, UserInitializer(initializer.n_slots+1, uses), this);
      } catch(...) {
	operator delete (term_base);
	throw;
      }
    }

    template<typename T>
    typename T::ResultType Context::allocate_distinct_term(const T& initializer) {
      typename T::ResultType rt = allocate_term(initializer);
      m_distinct_terms.push_back(*rt);
      return rt;
    }

    struct MetatypeTerm::Initializer : InitializerBase<MetatypeTerm> {
      static const std::size_t n_slots = 0;
      MetatypeTerm* init(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) MetatypeTerm(ui, context);
      }
    };

    Context::Context() {
      m_metatype.reset(allocate_term(MetatypeTerm::Initializer()));
    }

    struct Context::DistinctTermDisposer {
      void operator () (DistinctTerm *t) const {
	switch(t->m_term_type) {
	case Term::term_function: delete static_cast<FunctionTerm*>(t); break;
	case Term::term_global_variable: delete static_cast<GlobalVariableTerm*>(t); break;
	case Term::term_recursive: delete static_cast<RecursiveTerm*>(t); break;
	case Term::term_function_type: delete static_cast<FunctionTypeTerm*>(t); break;
	case Term::term_function_type_parameter: delete static_cast<FunctionTypeParameterTerm*>(t); break;
	default: PSI_FAIL("cannot dispose of unknown type");
	}
      }
    };

    Context::~Context() {
      m_distinct_terms.clear_and_dispose(DistinctTermDisposer());
    }

    struct RecursiveTerm::Initializer : InitializerBase<RecursiveTerm> {
      static const std::size_t n_slots = 1;
      Term *type;
      Initializer(Term *type_) : type(type_) {}
      RecursiveTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) RecursiveTerm(ui, context, type);
      }
    };

    RecursiveTerm* Context::new_recursive(Term *type) {
      return allocate_distinct_term(RecursiveTerm::Initializer(type));
    }

    struct GlobalVariableTerm::Initializer : InitializerBase<GlobalVariableTerm> {
      static const std::size_t n_slots = 0;
      Term *type;
      bool constant;
      Initializer(Term *type_, bool constant_) : type(type_), constant(constant_) {}
      GlobalVariableTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) GlobalVariableTerm(ui, context, type, constant);
      }
    };

    GlobalVariableTerm* Context::new_global_variable(Term *type, bool constant) {
      return allocate_distinct_term(GlobalVariableTerm::Initializer(type, constant));
    }

    std::size_t Context::term_hash(const Term *t) {
      switch (t->m_term_type) {
      case Term::term_functional:
	return static_cast<const FunctionalTerm*>(t)->m_hash;

      case Term::term_function_type_internal:
	return static_cast<const FunctionTypeInternalTerm*>(t)->m_hash;

      case Term::term_function_type_internal_parameter:
	return static_cast<const FunctionTypeInternalParameterTerm*>(t)->m_hash;

      default:
	return boost::hash_value(t);
      }
    }

    namespace {
      struct HashKey {
	std::size_t hash;
      };

      struct HashKeyHash {
	std::size_t operator () (const HashKey& k) const {
	  return k.hash;
	}
      };

      struct ParameterizedTermKey : HashKey {
	std::size_t n_parameters;
	Term *const* parameters;
      };

      struct FunctionalTermKey : ParameterizedTermKey {
	const FunctionalTermBackend *backend;
      };

      struct FunctionTypeInternalTermKey : ParameterizedTermKey {
	Term *result;
      };
    }

    struct Context::FunctionalTermKeyEquals {
      static void init_param_key(ParameterizedTermKey& key, std::size_t n_parameters, Term *const* parameters) {
	key.hash = 0;
	key.n_parameters = n_parameters;
	key.parameters = parameters;

	for (std::size_t i = 0; i < n_parameters; ++i)
	  boost::hash_combine(key.hash, term_hash(parameters[i]));
      }

      static void init_key(FunctionalTermKey& key, const FunctionalTermBackend *backend, std::size_t n_parameters, Term *const* parameters) {
	init_param_key(key, n_parameters, parameters);
	key.backend = backend;
	boost::hash_combine(key.hash, backend->hash_value());
      }

      bool operator () (const FunctionalTermKey& key, const FunctionalTerm& value) const {
	if ((key.hash != value.m_hash) || (key.n_parameters != value.n_parameters()))
	  return false;

	for (std::size_t i = 0; i < key.n_parameters; ++i) {
	  if (key.parameters[i] != value.parameter(i))
	    return false;
	}

	if (!key.backend->equals(*value.m_backend))
	  return false;

	return true;
      };
    };

    struct FunctionalTerm::Initializer {
      typedef FunctionalTerm* ResultType;

      std::size_t proto_offset, size, n_slots;
      FunctionalTermKey key;
      Term *type;

      Initializer(const FunctionalTermKey& key_, Term *type_) : key(key_), type(type_) {
	std::pair<std::size_t, std::size_t> backend_size_align = key.backend->size_align();
	PSI_ASSERT_MSG((backend_size_align.second & (backend_size_align.second - 1)) == 0, "alignment is not a power of two");
	proto_offset = struct_offset(0, sizeof(FunctionalTerm), backend_size_align.second);
	size = proto_offset + backend_size_align.first;
	n_slots = key.n_parameters;
      }

      FunctionalTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	FunctionalTermBackend *new_backend = key.backend->clone(ptr_offset(base, proto_offset));
	try {
	  return new (base) FunctionalTerm(ui, context, type, key.hash, new_backend, key.n_parameters, key.parameters);
	} catch(...) {
	  new_backend->~FunctionalTermBackend();
	  throw;
	}
      }
    };

    struct Context::FunctionalTermFactory {
      Context *self;
      FunctionalTermFactory(Context *self_) : self(self_) {}
      FunctionalTerm* operator () (const FunctionalTermKey& key) const {
	Term *type = key.backend->type(*self, key.n_parameters, key.parameters);
	return self->allocate_term(FunctionalTerm::Initializer(key, type));
      }
    };

    FunctionalTerm* Context::get_functional_internal(const FunctionalTermBackend& backend, std::size_t n_parameters, Term *const* parameters) {
      FunctionalTermKey key;
      FunctionalTermKeyEquals::init_key(key, &backend, n_parameters, parameters);
      return m_functional_terms.get(key, HashKeyHash(), FunctionalTermKeyEquals(), FunctionalTermFactory(this));
    }

    struct Context::FunctionTypeInternalTermKeyEquals {
      static void init_key(FunctionTypeInternalTermKey& key, Term *result, std::size_t n_parameters, Term *const* parameters) {
	FunctionalTermKeyEquals::init_param_key(key, n_parameters, parameters);
	key.result = result;
	boost::hash_combine(key.hash, term_hash(result));
      }

      bool operator () (const FunctionTypeInternalTermKey& key, const FunctionTypeInternalTerm& value) const {
	if ((key.hash != value.m_hash) || (key.n_parameters != value.n_parameters()))
	  return false;

	for (std::size_t i = 0; i < key.n_parameters; ++i) {
	  if (key.parameters[i] != value.function_parameter(i))
	    return false;
	}

	if (key.result != value.function_result())
	  return false;

	return true;
      };
    };

    struct FunctionTypeInternalTerm::Initializer : InitializerBase<FunctionTypeInternalTerm> {
      FunctionTypeInternalTermKey key;
      std::size_t n_slots;
      Initializer(const FunctionTypeInternalTermKey& key_) : key(key_), n_slots(key_.n_parameters+1) {
      }
      FunctionTypeInternalTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeInternalTerm(ui, context, key.result, key.n_parameters, key.parameters);
      }
    };	

    struct Context::FunctionTypeInternalTermFactory {
      Context *self;
      FunctionTypeInternalTermFactory(Context *self_) : self(self_) {}
      FunctionTypeInternalTerm* operator () (const FunctionTypeInternalTermKey& key) const {
	return self->allocate_term(FunctionTypeInternalTerm::Initializer(key));
      }
    };

    FunctionTypeInternalTerm* Context::get_function_type_internal(Term *result, std::size_t n_parameters, Term *const* parameters) {
      FunctionTypeInternalTermKey key;
      FunctionTypeInternalTermKeyEquals::init_key(key, result, n_parameters, parameters);
      return m_function_type_internal_terms.get(key, HashKeyHash(), FunctionTypeInternalTermKeyEquals(), FunctionTypeInternalTermFactory(this));
    }

    namespace {
      struct FunctionTypeInternalParameterTermKey : HashKey {
	std::size_t index;
	std::size_t depth;
      };
    }

    struct Context::FunctionTypeInternalParameterTermKeyEquals {
      static void init_key(FunctionTypeInternalParameterTermKey& key, std::size_t index, std::size_t depth) {
	key.hash = 0;
	key.index = index;
	key.depth = depth;
	boost::hash_combine(key.hash, index);
	boost::hash_combine(key.hash, depth);
      }

      bool operator () (const FunctionTypeInternalParameterTermKey& key, const FunctionTypeInternalParameterTerm& value) const {
	return (key.hash == value.m_hash) && (key.index == value.m_index) && (key.depth == value.m_depth);
      }
    };

    struct FunctionTypeInternalParameterTerm::Initializer : InitializerBase<FunctionTypeInternalParameterTerm> {
      static const std::size_t n_slots = 0;
      FunctionTypeInternalParameterTermKey key;
      Initializer(const FunctionTypeInternalParameterTermKey& key_) : key(key_) {
      }
      FunctionTypeInternalParameterTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeInternalParameterTerm(ui, context, key.hash, key.depth, key.index);
      }
    };

    struct Context::FunctionTypeInternalParameterTermFactory {
      Context *self;
      FunctionTypeInternalParameterTermFactory(Context *self_) : self(self_) {}
      FunctionTypeInternalParameterTerm* operator () (const FunctionTypeInternalParameterTermKey& key) const {
	return self->allocate_term(FunctionTypeInternalParameterTerm::Initializer(key));
      }
    };

    FunctionTypeInternalParameterTerm* Context::get_function_type_internal_parameter(std::size_t depth, std::size_t index) {
      FunctionTypeInternalParameterTermKey key;
      FunctionTypeInternalParameterTermKeyEquals::init_key(key, index, depth);
      return m_function_type_internal_parameter_terms.get(key, HashKeyHash(), FunctionTypeInternalParameterTermKeyEquals(),
							  FunctionTypeInternalParameterTermFactory(this));
    }

    bool Context::check_function_type_complete(Term *term, std::tr1::unordered_set<FunctionTypeTerm*>& functions)
    {
      if (!term->parameterized())
	return true;

      if (!check_function_type_complete(term->type(), functions))
	return false;

      switch(term->term_type()) {
      case Term::term_functional: {
	FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->parameter(i), functions))
	    return false;
	}
	return true;
      }

      case Term::term_function_type: {
	FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	functions.insert(cast_term);
	if (!check_function_type_complete(cast_term->function_result_type(), functions))
	  return false;
	for (std::size_t i = 0; i < cast_term->n_function_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->function_parameter(i)->type(), functions))
	    return false;
	}
	functions.erase(cast_term);
	return true;
      }

      case Term::term_function_type_parameter: {
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
      case Term::term_functional: {
	FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	Term *type = build_function_type_resolver_term(depth, cast_term->type(), functions);
	std::size_t n_parameters = cast_term->n_parameters();
	boost::scoped_array<Term*> parameters(new Term*[n_parameters]);
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++)
	  parameters[i] = build_function_type_resolver_term(depth, term, functions);
	return get_functional_internal_with_type(*cast_term->m_backend, type, n_parameters, parameters.get());
      }

      case Term::term_function_type: {
	FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	PSI_ASSERT(functions.find(cast_term) == functions.end());
	FunctionResolveStatus& status = functions[cast_term];
	status.depth = depth + 1;
	status.index = 0;

	std::size_t n_parameters = cast_term->n_function_parameters();
	boost::scoped_array<Term*> parameter_types(new Term*[n_parameters]);
	for (std::size_t i = 0; i < n_parameters; ++i) {
	  parameter_types[i] = build_function_type_resolver_term(depth+1, cast_term->function_parameter(i)->type(), functions);
	  status.index++;
	}

	Term *result_type = build_function_type_resolver_term(depth+1, cast_term->function_result_type(), functions);
	functions.erase(cast_term);

	return get_function_type_internal(result_type, n_parameters, parameter_types.get());
      }

      case Term::term_function_type_parameter: {
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

    struct FunctionTypeTerm::Initializer : InitializerBase<FunctionTypeTerm> {
      std::size_t n_slots;
      Term *result_type;
      std::size_t n_parameters;
      FunctionTypeParameterTerm *const* parameters;
      Initializer(Term *result_type_, std::size_t n_parameters_, FunctionTypeParameterTerm *const* parameters_)
	: n_slots(n_parameters+1), result_type(result_type_), n_parameters(n_parameters_), parameters(parameters_) {}
      FunctionTypeTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeTerm(ui, context, result_type, n_parameters, parameters);
      }
    };

    FunctionTypeTerm* Context::get_function_type(Term *result_type, std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters) {
      FunctionTypeTerm *term = allocate_distinct_term(FunctionTypeTerm::Initializer(result_type, n_parameters, parameters));

      for (std::size_t i = 0; i < n_parameters; ++i) {
	parameters[i]->m_index = i;
	parameters[i]->m_source = term;
      }

      // it's only possible to merge complete types, since incomplete
      // types depend on higher up terms which have not yet been
      // built.
      std::tr1::unordered_set<FunctionTypeTerm*> check_functions;
      if (!check_function_type_complete(term, check_functions))
	return term;

      term->m_parameterized = false;

      FunctionResolveMap functions;
      FunctionResolveStatus& status = functions[term];
      status.depth = 0;
      status.index = 0;

      boost::scoped_array<Term*> internal_parameter_types(new Term*[n_parameters]);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	internal_parameter_types[i] = build_function_type_resolver_term(0, parameters[i]->type(), functions);
	status.index++;
      }

      Term *internal_result_type = build_function_type_resolver_term(0, term->function_result_type(), functions);
      PSI_ASSERT((functions.erase(term), functions.empty()));

      FunctionTypeInternalTerm *internal = get_function_type_internal(internal_result_type, n_parameters, internal_parameter_types.get());
      if (internal->m_function_type) {
	// A matching type exists
	return internal->m_function_type;
      } else {
	internal->m_function_type = term;
	return term;
      }
    }

    struct FunctionTypeParameterTerm::Initializer : InitializerBase<FunctionTypeParameterTerm> {
      static const std::size_t n_slots = 1;
      Term *type;
      Initializer(Term *type_) : type(type_) {}
      FunctionTypeParameterTerm* init(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeParameterTerm(ui, context, type);
      }
    };

    FunctionTypeParameterTerm* Context::new_function_type_parameter(Term *type) {
      return allocate_distinct_term(FunctionTypeParameterTerm::Initializer(type));
    }

    namespace {
      template<typename T>
      class VisitQueue {
      public:
	bool empty() const {
	  return m_queue.empty();
	}

	void insert(const T& t) {
	  if (m_visited.insert(t).second)
	    m_queue.push_back(t);
	}

	T pop_value() {
	  T x = m_queue.back();
	  m_queue.pop_back();
	  return x;
	}

      private:
	std::tr1::unordered_set<T> m_visited;
	std::vector<T> m_queue;
      };

      void insert_if_abstract(VisitQueue<Term*>& queue, Term *term) {
	if (term->abstract())
	  queue.insert(term);
      }
    }

    bool Context::search_for_abstract(Term *term) {
      if (!term->abstract())
	return false;

      VisitQueue<Term*> visit_queue;
      visit_queue.insert(term);
      while(!visit_queue.empty()) {
	Term *term = visit_queue.pop_value();

	PSI_ASSERT(term->abstract());

	switch (term->term_type()) {
	case Term::term_functional: {
	  FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	  insert_if_abstract(visit_queue, cast_term->type());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    insert_if_abstract(visit_queue, cast_term->parameter(i));
	  break;
	}

	case Term::term_recursive: {
	  RecursiveTerm *cast_term = static_cast<RecursiveTerm*>(term);
	  if (!cast_term->value())
	    return true;
	  insert_if_abstract(visit_queue, cast_term->value());
	  break;
	}

	case Term::term_function_type: {
	  FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	  insert_if_abstract(visit_queue, cast_term->function_result_type());
	  for (std::size_t i = 0; i < cast_term->n_function_parameters(); ++i)
	    insert_if_abstract(visit_queue, cast_term->function_parameter(i)->type());
	  break;
	}

	case Term::term_function_type_parameter: {
	  // Don't need to check these since they're covered by the
	  // function_type case
	  break;
	}

	default:
	  PSI_FAIL("unexpected abstract term type");
	}
      }

      return false;
    }

    void Context::clear_and_queue_if_abstract(std::vector<Term*>& queue, Term *t) {
      if (t->abstract()) {
	t->m_abstract = false;
	queue.push_back(t);
      }
    }

    void Context::clear_abstract(Term *term, std::vector<Term*>& queue) {
      if (!term->abstract())
	return;

      PSI_ASSERT(queue.empty());
      queue.push_back(term);
      while(!queue.empty()) {
	Term *term = queue.back();
	queue.pop_back();

	switch (term->term_type()) {
	case Term::term_functional: {
	  FunctionalTerm *cast_term = static_cast<FunctionalTerm*>(term);
	  clear_and_queue_if_abstract(queue, cast_term->type());
	  for (std::size_t i = 0; i < cast_term->n_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term->parameter(i));
	  break;
	}

	case Term::term_recursive: {
	  RecursiveTerm *cast_term = static_cast<RecursiveTerm*>(term);
	  PSI_ASSERT(cast_term->value());
	  clear_and_queue_if_abstract(queue, cast_term->value());
	  break;
	}

	case Term::term_function_type: {
	  FunctionTypeTerm *cast_term = static_cast<FunctionTypeTerm*>(term);
	  clear_and_queue_if_abstract(queue, cast_term->function_result_type());
	  for (std::size_t i = 0; i < cast_term->n_function_parameters(); ++i)
	    clear_and_queue_if_abstract(queue, cast_term->function_parameter(i)->type());
	  break;
	}

	case Term::term_function_type_parameter: {
	  // Don't need to check these since they're covered by the
	  // function_type case
	  break;
	}

	default:
	  PSI_FAIL("unexpected abstract term type");
	}
      }
    }

    void Context::resolve_recursive(RecursiveTerm *recursive, Term *to) {
      if (recursive->type() != to->type())
	throw std::logic_error("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
	throw std::logic_error("cannot resolve recursive term to parameterized term");

      if (recursive->value())
	throw std::logic_error("resolving a recursive term which has already been resolved");

      recursive->set_parameter(0, to);

      if (!search_for_abstract(recursive)) {
	std::vector<Term*> downward_queue;
	clear_abstract(recursive, downward_queue);

	std::vector<Term*> upward_queue;
	upward_queue.push_back(recursive);
	while (!upward_queue.empty()) {
	  Term *t = upward_queue.back();
	  upward_queue.pop_back();
	  for (UserIterator it = t->users_begin(); it != t->users_end(); ++it) {
	    Term *parent = checked_pointer_static_cast<Term>(it.get());
	    if (parent->abstract() && !search_for_abstract(parent)) {
	      clear_abstract(parent, downward_queue);
	      upward_queue.push_back(parent);
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

    void* Context::term_jit(Term *term) {
      if ((term->m_term_type != Term::term_global_variable) &&
	  (term->m_term_type != Term::term_function))
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

    Term::Term(const UserInitializer& ui, Context *context, TermType term_type, bool abstract, bool parameterized, Term *type)
      : User(ui), m_context(context), m_term_type(term_type), m_abstract(abstract), m_parameterized(parameterized) {

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

    bool Term::any_abstract(std::size_t n, Term *const* terms) {
      for (std::size_t i = 0; i < n; ++i) {
	if (terms[i]->abstract())
	  return true;
      }
      return false;
    }

    bool Term::any_parameterized(std::size_t n, Term *const* terms) {
      for (std::size_t i = 0; i < n; ++i) {
	if (terms[i]->parameterized())
	  return true;
      }
      return false;
    }

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

    MetatypeTerm::MetatypeTerm(const UserInitializer& ui, Context* context)
      : Term(ui, context, term_metatype, false, false, NULL) {
    }

#if 0
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
      PSI_ASSERT_MSG(size->getType()->isIntegerTy(64) && align->getType()->isIntegerTy(64),
		     "size and align members of Metatype must both be i64");
      PSI_ASSERT_MSG(!llvm::cast<llvm::ConstantInt>(align)->equalsInt(0), "align cannot be zero");
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
#endif

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

    DistinctTerm::DistinctTerm(const UserInitializer& ui, Context* context, TermType term_type, bool abstract, bool parameterized, Term *type) 
      : Term(ui, context, term_type, abstract, parameterized, type) {
    }

    FunctionalTerm::FunctionalTerm(const UserInitializer& ui, Context *context, Term *type,
				   std::size_t hash, FunctionalTermBackend *backend,
				   std::size_t n_parameters, Term *const* parameters)
      : Term(ui, context, term_functional,
	     type->abstract() || any_abstract(n_parameters, parameters),
	     type->parameterized() || any_parameterized(n_parameters, parameters),
	     type),
	m_hash(hash),
	m_backend(backend) {
      for (std::size_t i = 0; i < n_parameters; ++i)
	set_parameter(i, parameters[i]);
    }

    FunctionalTerm::~FunctionalTerm() {
      m_backend->~FunctionalTermBackend();
    }

    bool FunctionTypeTerm::any_parameter_abstract(std::size_t n, FunctionTypeParameterTerm *const* terms) {
      for (std::size_t i = 0; i < n; ++i) {
	if (terms[i]->abstract())
	  return true;
      }
      return false;
    }

    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui, Context *context,
				       Term *result_type, std::size_t n_parameters, FunctionTypeParameterTerm *const* parameters)
      : DistinctTerm(ui, context, term_function_type,
		     result_type->abstract() || any_parameter_abstract(n_parameters, parameters), true,
		     context->get_metatype()) {
      set_parameter(0, result_type);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	set_parameter(i+1, parameters[i]);
      }
    }

    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, Term *type)
      : DistinctTerm(ui, context, term_function_type_parameter, type->abstract(), true, type), m_source(0), m_index(0) {
    }
  }
}
