#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"
#include "Primitive.hpp"

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    RecursiveParameterTerm::RecursiveParameterTerm(const UserInitializer& ui, Context *context, Term* type, bool phantom)
      : Term(ui, context, term_recursive_parameter, true, false, phantom, term_source(type), type) {
    }

    class RecursiveParameterTerm::Initializer : public InitializerBase<RecursiveParameterTerm> {
    public:
      Initializer(Term* type, bool phantom) : m_type(type), m_phantom(phantom) {}

      RecursiveParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) RecursiveParameterTerm(ui, context, m_type, m_phantom);
      }

      std::size_t n_uses() const {return 0;}

    private:
      Term* m_type;
      bool m_phantom;
    };

    /**
     * \brief Create a new parameter for a recursive term.
     *
     * \param type The term's type.
     *
     * \param phantom Whether this term should be created as a phantom
     * term. This mechanism is used to inform the compiler which
     * parameters can have phantom values in them without making the
     * overall value a phantom (unless it is always a phantom).
     */
    RecursiveParameterTerm* Context::new_recursive_parameter(Term* type, bool phantom) {
      return allocate_term(RecursiveParameterTerm::Initializer(type, phantom));
    }

    RecursiveTerm::RecursiveTerm(const UserInitializer& ui, Context *context, Term* result_type,
				 Term *source, ArrayPtr<RecursiveParameterTerm*const> parameters,
                                 bool phantom)
      : Term(ui, context, term_recursive, true, false, phantom, source, NULL) {
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < parameters.size(); ++i) {
	set_base_parameter(i+2, parameters[i]);
      }
    }

    class RecursiveTerm::Initializer : public InitializerBase<RecursiveTerm> {
    public:
      Initializer(Term *source, Term* type, ArrayPtr<RecursiveParameterTerm*const> parameters, bool phantom)
	: m_source(source), m_type(type), m_parameters(parameters), m_phantom(phantom) {
      }

      RecursiveTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) RecursiveTerm(ui, context, m_type, m_source, m_parameters, m_phantom);
      }

      std::size_t n_uses() const {return 1;}

    private:
      Term *m_source;
      Term* m_type;
      ArrayPtr<RecursiveParameterTerm*const> m_parameters;
      bool m_phantom;
    };

    /**
     * \brief Create a new recursive term.
     *
     * \param phantom Whether all applications of this term are
     * considered phantom; in this case the value assigned to this
     * term may itself be a phantom.
     */
    RecursiveTerm* Context::new_recursive(Term *source,
                                          Term* result_type,
                                          ArrayPtr<Term*const> parameter_types,
                                          bool phantom) {
      if (source_dominated(result_type->source(), source))
        goto throw_dominator;

      for (std::size_t i = 0; i < parameter_types.size(); ++i) {
        if (source_dominated(parameter_types[i]->source(), source))
          goto throw_dominator;
      }

      if (true) {
        ScopedTermPtrArray<RecursiveParameterTerm> parameters(parameter_types.size());
        for (std::size_t i = 0; i < parameters.size(); ++i)
          parameters[i] = new_recursive_parameter(parameter_types[i]);
        return allocate_term(RecursiveTerm::Initializer(source, result_type, parameters.array(), phantom));
      } else {
      throw_dominator:
        throw std::logic_error("block specified for recursive term is not dominated by parameter and result type blocks");
      }
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveTerm::resolve(Term* term) {
      return context().resolve_recursive(this, term);
    }

    ApplyTerm* RecursiveTerm::apply(ArrayPtr<Term*const> parameters) {
      return context().apply_recursive(this, parameters);
    }

    namespace {
      bool apply_is_phantom(RecursiveTerm *recursive, ArrayPtr<Term*const> parameters) {
        if (recursive->phantom())
          return true;

        PSI_ASSERT(recursive->n_parameters() == parameters.size());
        for (std::size_t i = 0; i < parameters.size(); i++) {
          if (!recursive->parameter(i)->phantom() && parameters[i]->phantom())
            return true;
        }

        return false;
      }
    }

    ApplyTerm::ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
			 ArrayPtr<Term*const> parameters, std::size_t hash)
      : HashTerm(ui, context, term_apply,
                 recursive->abstract() || any_abstract(parameters), false,
                 apply_is_phantom(recursive, parameters),
                 common_source(recursive->source(), common_source(parameters)),
                 recursive->result_type(), hash) {
      set_base_parameter(0, recursive);
      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i+1, parameters[i]);
    }

    class ApplyTerm::Setup : public InitializerBase<ApplyTerm> {
    public:
      Setup(RecursiveTerm *recursive, ArrayPtr<Term*const> parameters)
	: m_recursive(recursive), m_parameters(parameters) {

        m_hash = 0;
	boost::hash_combine(m_hash, recursive->hash_value());
	for (std::size_t i = 0; i < parameters.size(); ++i)
	  boost::hash_combine(m_hash, parameters[i]->hash_value());
      }

      void prepare_initialize(Context*) {
      }

      ApplyTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) ApplyTerm(ui, context, m_recursive, m_parameters, m_hash);
      }

      std::size_t n_uses() const {return m_parameters.size() + 1;}
      std::size_t hash() const {return m_hash;}

      bool equals(const HashTerm& term) const {
	if ((m_hash != term.m_hash) || (term.term_type() != term_apply))
	  return false;

	const ApplyTerm& cast_term = checked_cast<const ApplyTerm&>(term);

	if (m_parameters.size() != cast_term.n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_parameters.size(); ++i) {
	  if (m_parameters[i] != cast_term.parameter(i))
	    return false;
	}

	return true;
      }

    private:
      RecursiveTerm *m_recursive;
      ArrayPtr<Term*const> m_parameters;
      std::size_t m_hash;
    };

    ApplyTerm* Context::apply_recursive(RecursiveTerm* recursive,
                                        ArrayPtr<Term*const> parameters) {
      ApplyTerm::Setup setup(recursive, parameters);
      return hash_term_get(setup);
    }

    Term* ApplyTerm::unpack() const {
      throw std::logic_error("not implemented");
    }

    namespace {
      void insert_if_abstract(std::vector<Term*>& queue, std::tr1::unordered_set<Term*>& set, Term* term) {
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

    void Context::clear_and_queue_if_abstract(std::vector<Term*>& queue, Term* t) {
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
    void Context::resolve_recursive(RecursiveTerm* recursive, Term* to) {
      if (recursive->type() != to->type())
	throw std::logic_error("mismatch between recursive term type and resolving term type");

      if (to->parameterized())
	throw std::logic_error("cannot resolve recursive term to parameterized term");

      if (recursive->result())
	throw std::logic_error("resolving a recursive term which has already been resolved");

      if (!source_dominated(to->source(), recursive->source()))
        throw std::logic_error("term used to resolve recursive term is not in scope");

      if (to->phantom() && !recursive->phantom())
        throw std::logic_error("non-phantom recursive term cannot be resolved to a phantom term");

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
  }
}
