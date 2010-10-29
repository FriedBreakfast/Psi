#include "Core.hpp"
#include "Functional.hpp"
#include "Utility.hpp"

#include <stdexcept>

#include <boost/scoped_array.hpp>

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
      return allocate_term(this, RecursiveParameterTerm::Initializer(type));
    }

    RecursiveTerm::RecursiveTerm(const UserInitializer& ui, Context *context, TermRef<> result_type,
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
      Initializer(bool global, TermRef<> type, std::size_t n_parameters, RecursiveParameterTerm *const* parameters)
	: m_global(global), m_type(type), m_n_parameters(n_parameters), m_parameters(parameters) {
      }

      RecursiveTerm* initialize(void *base, const UserInitializer& ui, Context* context) const {
	return new (base) RecursiveTerm(ui, context, m_type, m_global, m_n_parameters, m_parameters);
      }

      std::size_t n_uses() const {return 1;}

    private:
      bool m_global;
      TermRef<> m_type;
      std::size_t m_n_parameters;
      RecursiveParameterTerm *const* m_parameters;
    };

    /**
     * \brief Create a new recursive term.
     */
    TermPtr<RecursiveTerm> Context::new_recursive(bool global, TermRef<> result_type,
						  std::size_t n_parameters,
						  Term *const* parameter_types) {
      boost::scoped_array<TermPtr<RecursiveParameterTerm> > parameters(new TermPtr<RecursiveParameterTerm>[n_parameters]);
      boost::scoped_array<RecursiveParameterTerm*> parameters_ptr(new RecursiveParameterTerm*[n_parameters]);
      for (std::size_t i = 0; i < n_parameters; ++i) {
	parameters[i] = new_recursive_parameter(parameter_types[i]);
        parameters_ptr[i] = parameters[i].get();
      }
      return TermPtr<RecursiveTerm>(allocate_term(this, RecursiveTerm::Initializer(global, result_type.get(), n_parameters, parameters_ptr.get())));
    }

    /**
     * \brief Resolve this term to its actual value.
     */
    void RecursiveTerm::resolve(TermRef<> term) {
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

    TermPtr<ApplyTerm> Context::apply_recursive(TermRef<RecursiveTerm> recursive,
						std::size_t n_parameters,
						Term *const* parameters) {
      return TermPtr<ApplyTerm>(allocate_term(this, ApplyTerm::Initializer(recursive.get(), n_parameters, parameters)));
    }

    TermPtr<> ApplyTerm::unpack() const {
      throw std::logic_error("not implemented");
    }
  }
}
