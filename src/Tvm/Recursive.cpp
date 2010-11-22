#include "Core.hpp"
#include "Recursive.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Utility.hpp"
#include "Primitive.hpp"

#include <stdexcept>

namespace Psi {
  namespace Tvm {
    RecursiveParameterTerm::RecursiveParameterTerm(const UserInitializer& ui, Context *context, TermRef<> type)
      : Term(ui, context, term_recursive_parameter, true, false, type->global(), type) {
    }

    class RecursiveParameterTerm::Initializer : public InitializerBase<RecursiveParameterTerm> {
    public:
      Initializer(TermRef<> type) : m_type(type) {}

      RecursiveParameterTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	return new (base) RecursiveParameterTerm(ui, context, m_type);
      }

      std::size_t n_uses() const {return 0;}

    private:
      TermRef<> m_type;
    };

    TermPtr<RecursiveParameterTerm> Context::new_recursive_parameter(TermRef<> type) {
      return allocate_term(RecursiveParameterTerm::Initializer(type));
    }

    RecursiveTerm::RecursiveTerm(const UserInitializer& ui, Context *context, TermRef<> result_type,
				 bool global, TermRefArray<RecursiveParameterTerm> parameters)
      : Term(ui, context, term_recursive, true, false, global, context->get_metatype().get()) {
      PSI_ASSERT(!global || (result_type->global() && all_global(parameters)));
      set_base_parameter(0, result_type);
      for (std::size_t i = 0; i < parameters.size(); ++i) {
	set_base_parameter(i+2, parameters[i]);
      }
    }

    class RecursiveTerm::Initializer : public InitializerBase<RecursiveTerm> {
    public:
      Initializer(bool global, TermRef<> type, TermRefArray<RecursiveParameterTerm> parameters)
	: m_global(global), m_type(type), m_parameters(parameters) {
      }

      RecursiveTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) RecursiveTerm(ui, context, m_type, m_global, m_parameters);
      }

      std::size_t n_uses() const {return 1;}

    private:
      bool m_global;
      TermRef<> m_type;
      TermRefArray<RecursiveParameterTerm> m_parameters;
    };

    /**
     * \brief Create a new recursive term.
     */
    TermPtr<RecursiveTerm> Context::new_recursive(bool global, TermRef<> result_type,
						  TermRefArray<> parameter_types) {
      TermPtrArray<RecursiveParameterTerm> parameters(parameter_types.size());
      for (std::size_t i = 0; i < parameters.size(); ++i)
	parameters.set(i, new_recursive_parameter(parameter_types[i]));
      return allocate_term(RecursiveTerm::Initializer(global, result_type.get(), parameters));
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveTerm::resolve(TermRef<> term) {
      return context().resolve_recursive(this, term);
    }

    TermPtr<ApplyTerm> RecursiveTerm::apply(TermRefArray<> parameters) {
      return TermPtr<ApplyTerm>(context().apply_recursive(this, parameters));
    }

    ApplyTerm::ApplyTerm(const UserInitializer& ui, Context *context, RecursiveTerm *recursive,
			 TermRefArray<> parameters)
      : Term(ui, context, term_apply,
	     recursive->abstract() || any_abstract(parameters), false,
	     recursive->global() && all_global(parameters),
	     context->get_metatype().get()) {
      set_base_parameter(0, recursive);
      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i+1, parameters[i]);
    }

    class ApplyTerm::Initializer : public InitializerBase<ApplyTerm> {
    public:
      Initializer(RecursiveTerm *recursive, TermRefArray<> parameters)
	: m_recursive(recursive), m_parameters(parameters) {
      }

      ApplyTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) ApplyTerm(ui, context, m_recursive, m_parameters);
      }

      std::size_t n_uses() const {return m_parameters.size() + 1;}

    private:
      RecursiveTerm *m_recursive;
      TermRefArray<> m_parameters;
    };

    TermPtr<ApplyTerm> Context::apply_recursive(TermRef<RecursiveTerm> recursive,
						TermRefArray<> parameters) {
      return allocate_term(ApplyTerm::Initializer(recursive.get(), parameters));
    }

    TermPtr<> ApplyTerm::unpack() const {
      throw std::logic_error("not implemented");
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
  }
}
