#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"

#include <stdexcept>

#include <boost/smart_ptr/scoped_array.hpp>

namespace Psi {
  namespace Tvm {
    FunctionTypeTerm::FunctionTypeTerm(const UserInitializer& ui, Context *context, Term *result_type, std::size_t n_parameters,
				       FunctionTypeParameterTerm *const* parameters, CallingConvention calling_convention)
      : Term(ui, context, term_function_type,
	     result_type->abstract() || any_abstract(n_parameters, parameters), true,
	     result_type->global() && all_global(n_parameters, parameters),
	     context->get_metatype().get()),
	m_calling_convention(calling_convention) {
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	set_base_parameter(i+1, parameters[i]);
      }
    }

    class FunctionTypeTerm::Initializer : public InitializerBase<FunctionTypeTerm> {
    public:
      Initializer(Term *result_type, std::size_t n_parameters,
		  FunctionTypeParameterTerm *const* parameters,
		  CallingConvention calling_convention)
	: m_result_type(result_type), m_n_parameters(n_parameters),
	  m_parameters(parameters), m_calling_convention(calling_convention) {
      }

      std::size_t n_uses() const {
	return m_n_parameters + 1;
      }

      FunctionTypeTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeTerm(ui, context, m_result_type,
					   m_n_parameters, m_parameters, m_calling_convention);
      }

    private:
      Term *m_result_type;
      std::size_t m_n_parameters;
      FunctionTypeParameterTerm *const* m_parameters;
      CallingConvention m_calling_convention;
    };

    /**
     * Check whether part of function type term is complete,
     * i.e. whether there are still function parameters which have
     * to be resolved by further function types (this happens in the
     * case of nested function types).
     */
    bool Context::check_function_type_complete(TermRef<> term, std::tr1::unordered_set<FunctionTypeTerm*>& functions)
    {
      if (!term->parameterized())
	return true;

      if (!check_function_type_complete(term->type(), functions))
	return false;

      switch(term->term_type()) {
      case term_functional: {
	FunctionalTerm *cast_term = checked_cast<FunctionalTerm*>(term.get());
	for (std::size_t i = 0; i < cast_term->n_parameters(); i++) {
	  if (!check_function_type_complete(cast_term->parameter(i), functions))
	    return false;
	}
	return true;
      }

      case term_function_type: {
	FunctionTypeTerm *cast_term = checked_cast<FunctionTypeTerm*>(term.get());
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
	FunctionTypeParameterTerm *cast_term = checked_cast<FunctionTypeParameterTerm*>(term.get());
	TermPtr<FunctionTypeTerm> source = cast_term->source();
	if (!source)
	  return false;

	if (functions.find(source.get()) != functions.end())
	  throw std::logic_error("type of function parameter appeared outside of function type definition");

	return true;
      }

      default:
	// all terms should either be amongst the handled cases or complete
	PSI_FAIL("unknown term type");
      }
    }

    TermPtr<> Context::build_function_type_resolver_term(std::size_t depth, TermRef<> term, FunctionResolveMap& functions) {
      if (!term->parameterized())
	return TermPtr<>(term.get());

      switch(term->term_type()) {
      case term_functional: {
	FunctionalTerm& cast_term = checked_cast<FunctionalTerm&>(*term);
        TermPtr<> type = build_function_type_resolver_term(depth, cast_term.type(), functions);
	std::size_t n_parameters = cast_term.n_parameters();
        boost::scoped_array<TermPtr<> > parameters(new TermPtr<>[n_parameters]);
	boost::scoped_array<Term*> parameters_ptr(new Term*[n_parameters]);
	for (std::size_t i = 0; i < cast_term.n_parameters(); i++) {
	  parameters[i] = build_function_type_resolver_term(depth, term, functions);
          parameters_ptr[i] = parameters[i].get();
        }
	return get_functional_internal_with_type(*cast_term.m_backend, type, n_parameters, parameters_ptr.get());
      }

      case term_function_type: {
	FunctionTypeTerm& cast_term = checked_cast<FunctionTypeTerm&>(*term);
	PSI_ASSERT(functions.find(&cast_term) == functions.end());
	FunctionResolveStatus& status = functions[&cast_term];
	status.depth = depth + 1;
	status.index = 0;

	std::size_t n_parameters = cast_term.n_parameters();
	boost::scoped_array<TermPtr<> > parameter_types(new TermPtr<>[n_parameters]);
	boost::scoped_array<Term*> parameter_types_ptr(new Term*[n_parameters]);
	for (std::size_t i = 0; i < n_parameters; ++i) {
	  parameter_types[i] = build_function_type_resolver_term(depth+1, cast_term.parameter(i)->type(), functions);
          parameter_types_ptr[i] = parameter_types[i].get();
	  status.index++;
	}

	TermPtr<> result_type = build_function_type_resolver_term(depth+1, cast_term.result_type(), functions);
	functions.erase(&cast_term);

	return get_function_type_internal(result_type, n_parameters, parameter_types_ptr.get(), cast_term.calling_convention());
      }

      case term_function_type_parameter: {
	FunctionTypeParameterTerm& cast_term = checked_cast<FunctionTypeParameterTerm&>(*term);
	TermPtr<FunctionTypeTerm> source = cast_term.source();

	FunctionResolveMap::iterator it = functions.find(source.get());
	PSI_ASSERT(it != functions.end());

	if (cast_term.index() >= it->second.index)
	  throw std::logic_error("function type parameter definition refers to value of later parameter");

        TermPtr<> type = build_function_type_resolver_term(depth, cast_term.type(), functions);

	return get_function_type_internal_parameter(type, depth - it->second.depth, cast_term.index());
      }

      default:
	// all terms should either be amongst the handled cases or complete
	PSI_FAIL("unknown term type");
      }
    }

    /**
     * Get a function type term.
     */
    TermPtr<FunctionTypeTerm> Context::get_function_type(CallingConvention calling_convention,
							 TermRef<> result_type, std::size_t n_parameters,
							 FunctionTypeParameterTerm *const* parameters) {
      for (std::size_t i = 0; i < n_parameters; ++i)
	PSI_ASSERT(!parameters[i]->source());

      TermPtr<FunctionTypeTerm> term(allocate_term(this, FunctionTypeTerm::Initializer(result_type.get(), n_parameters, parameters, calling_convention)));

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

      boost::scoped_array<TermPtr<> > internal_parameter_types(new TermPtr<>[n_parameters]);
      boost::scoped_array<Term*> internal_parameter_types_ptr(new Term*[n_parameters]);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	internal_parameter_types[i] = build_function_type_resolver_term(0, parameters[i]->type(), functions);
        internal_parameter_types_ptr[i] = internal_parameter_types[i].get();
	status.index++;
      }

      TermPtr<> internal_result_type = build_function_type_resolver_term(0, term->result_type(), functions);
      PSI_ASSERT((functions.erase(term.get()), functions.empty()));

      TermPtr<FunctionTypeInternalTerm> internal = get_function_type_internal(internal_result_type, n_parameters, internal_parameter_types_ptr.get(), calling_convention);
      if (internal->get_function_type()) {
	// A matching type exists
	return TermPtr<FunctionTypeTerm>(internal->get_function_type());
      } else {
	internal->set_function_type(term.get());
	return term;
      }
    }

    /**
     * \brief Get a function type with fixed argument types.
     */
    TermPtr<FunctionTypeTerm> Context::get_function_type_fixed(CallingConvention calling_convention,
							       TermRef<> result,
							       std::size_t n_parameters,
							       Term *const* parameter_types) {
      boost::scoped_array<TermPtr<FunctionTypeParameterTerm> > parameters(new TermPtr<FunctionTypeParameterTerm>[n_parameters]);
      boost::scoped_array<FunctionTypeParameterTerm*> parameters_ptr(new FunctionTypeParameterTerm*[n_parameters]);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	parameters[i] = new_function_type_parameter(parameter_types[i]);
	parameters_ptr[i] = parameters[i].get();
      }
      return get_function_type(calling_convention, result, n_parameters, parameters_ptr.get());
    }

    FunctionTypeParameterTerm::FunctionTypeParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type)
      : Term(ui, context, term_function_type_parameter, type->abstract(), true, type->global(), type),
	m_index(0) {
    }

    class FunctionTypeParameterTerm::Initializer : public InitializerBase<FunctionTypeParameterTerm> {
    public:
      Initializer(TermRef<> type) : m_type(type) {}

      std::size_t n_uses() const {
	return 1;
      }

      FunctionTypeParameterTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTypeParameterTerm(ui, context, m_type);
      }

    private:
      TermRef<> m_type;
    };

    TermPtr<FunctionTypeParameterTerm> Context::new_function_type_parameter(TermRef<> type) {
      return TermPtr<FunctionTypeParameterTerm>(allocate_term(this, FunctionTypeParameterTerm::Initializer(type.get())));
    }

    FunctionTypeInternalTerm::FunctionTypeInternalTerm(const UserInitializer& ui, Context *context, std::size_t hash, TermRef<> result_type, std::size_t n_parameters, Term *const* parameter_types, CallingConvention calling_convention)
      : HashTerm(ui, context, term_function_type_internal,
		 result_type->abstract() || any_abstract(n_parameters, parameter_types), true,
		 result_type->global() && all_global(n_parameters, parameter_types),
		 context->get_metatype().get(), hash),
	m_calling_convention(calling_convention) {
      set_base_parameter(1, result_type);
      for (std::size_t i = 0; i < n_parameters; i++)
	set_base_parameter(i+2, parameter_types[i]);
    }

    class FunctionTypeInternalTerm::Setup : public InitializerBase<FunctionTypeInternalTerm> {
    public:
      Setup(TermRef<> result_type, std::size_t n_parameters, Term *const* parameter_types,
	    CallingConvention calling_convention)
	: m_n_parameters(n_parameters),
	  m_parameter_types(parameter_types),
	  m_result_type(result_type),
	  m_calling_convention(calling_convention) {
	m_hash = 0;
	boost::hash_combine(m_hash, result_type->hash_value());
	for (std::size_t i = 0; i < n_parameters; ++i)
	  boost::hash_combine(m_hash, parameter_types[i]->hash_value());
	boost::hash_combine(m_hash, calling_convention);
      }

      void prepare_initialize(Context*) {
      }

      FunctionTypeInternalTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionTypeInternalTerm(ui, context, m_hash, m_result_type, m_n_parameters, m_parameter_types, m_calling_convention);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t n_uses() const {
	return m_n_parameters + 2;
      }

      bool equals(const HashTerm& term) const {
	if ((m_hash != term.m_hash) || (term.term_type() != term_function_type_internal))
	  return false;

	const FunctionTypeInternalTerm& cast_term =
	  checked_cast<const FunctionTypeInternalTerm&>(term);

	if (m_n_parameters != cast_term.n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_n_parameters; ++i) {
	  if (m_parameter_types[i] != cast_term.parameter_type(i).get())
	    return false;
	}

	if (m_result_type.get() != cast_term.result_type().get())
	  return false;

	if (m_calling_convention != cast_term.calling_convention())
	  return false;

	return true;
      }

    private:
      std::size_t m_hash;
      std::size_t m_n_parameters;
      Term *const* m_parameter_types;
      TermRef<> m_result_type;
      CallingConvention m_calling_convention;
    };

    TermPtr<FunctionTypeInternalTerm> Context::get_function_type_internal(TermRef<> result, std::size_t n_parameters, Term *const* parameter_types, CallingConvention calling_convention) {
      FunctionTypeInternalTerm::Setup setup(result, n_parameters, parameter_types, calling_convention);
      return hash_term_get(setup);
    }

    FunctionTypeInternalParameterTerm::FunctionTypeInternalParameterTerm(const UserInitializer& ui, Context *context, std::size_t hash, TermRef<> type, std::size_t depth, std::size_t index)
      : HashTerm(ui, context, term_function_type_internal_parameter,
		 type->abstract(), true, type->global(), type, hash),
        m_depth(depth),
        m_index(index) {
    }

    class FunctionTypeInternalParameterTerm::Setup
      : public InitializerBase<FunctionTypeInternalParameterTerm> {
    public:
      Setup(TermRef<> type, std::size_t depth, std::size_t index)
	: m_type(type), m_depth(depth), m_index(index) {
	m_hash = 0;
        boost::hash_combine(m_hash, type->hash_value());
	boost::hash_combine(m_hash, depth);
	boost::hash_combine(m_hash, m_index);
      }

      void prepare_initialize(Context*) {
      }

      FunctionTypeInternalParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionTypeInternalParameterTerm(ui, context, m_hash, m_type, m_depth, m_index);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t n_uses() const {
	return 0;
      }

      bool equals(const HashTerm& term) const {
	if ((m_hash != term.m_hash) ||
            (term.term_type() != term_function_type_internal_parameter) ||
            (m_type.get() != term.type().get()))
	  return false;

	const FunctionTypeInternalParameterTerm& cast_term =
	  checked_cast<const FunctionTypeInternalParameterTerm&>(term);

	if (m_depth != cast_term.m_depth)
	  return false;

	if (m_index != cast_term.m_index)
	  return false;

	return true;
      }

    private:
      TermRef<> m_type;
      std::size_t m_depth;
      std::size_t m_index;
      std::size_t m_hash;
    };

    TermPtr<FunctionTypeInternalParameterTerm> Context::get_function_type_internal_parameter(TermRef<> type, std::size_t depth, std::size_t index) {
      FunctionTypeInternalParameterTerm::Setup setup(type, depth, index);
      return hash_term_get(setup);
    }

    BlockTerm::BlockTerm(const UserInitializer& ui, Context *context, FunctionTerm *function)
      : Term(ui, context, term_block, false, false, false, context->get_block_type()) {
      set_base_parameter(0, function);

      m_use_count_ptr = 1;
      m_use_count.ptr = function->term_use_count();
      // remove ref created by set_base_parameter
      --*m_use_count.ptr;
    }

    class BlockTerm::Initializer : public InitializerBase<BlockTerm> {
    public:
      Initializer(FunctionTerm *function) : m_function(function) {
      }

      BlockTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) BlockTerm(ui, context, m_function);
      }

      std::size_t n_uses() const {
	return 1;
      }

    private:
      FunctionTerm *m_function;
    };

    TermPtr<> BlockTerm::new_instruction_internal(const InstructionTermBackend& backend, std::size_t n_parameters, Term *const* parameters) {
    }

    FunctionTerm::FunctionTerm(const UserInitializer& ui, Context *context, TermRef<FunctionTypeTerm> type)
      : GlobalTerm(ui, context, term_function, type),
	m_calling_convention(type->calling_convention()) {
      BlockTerm *bl = allocate_term(context, BlockTerm::Initializer(this));
      m_blocks.push_back(*bl);
    }

    class FunctionTerm::Initializer : public InitializerBase<FunctionTerm> {
    public:
      Initializer(TermRef<FunctionTypeTerm> type) : m_type(type) {
      }

      FunctionTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) FunctionTerm(ui, context, m_type);
      }

      std::size_t n_uses() const {
	return 0;
      }

    private:
      TermRef<FunctionTypeTerm> m_type;
    };

    /**
     * \brief Create a new function.
     */
    TermPtr<FunctionTerm> Context::new_function(TermRef<FunctionTypeTerm> type) {
      return TermPtr<FunctionTerm>(allocate_term(this, FunctionTerm::Initializer(type)));
    }

    FunctionParameterTerm::FunctionParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type)
      : Term(ui, context, term_function_parameter, false, false, type->global(), type) {
      PSI_ASSERT(!type->parameterized() && !type->abstract());
    }

    class FunctionParameterTerm::Initializer : public InitializerBase<FunctionParameterTerm> {
    public:
      Initializer(TermRef<> type) : m_type(type) {
      }

      std::size_t n_uses() const {
	return 0;
      }

      FunctionParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) FunctionParameterTerm(ui, context, m_type);
      }

    private:
      TermRef<> m_type;
    };
  }
}
