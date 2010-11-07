#include "Core.hpp"
#include "Recursive.hpp"
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
      return allocate_term(this, RecursiveParameterTerm::Initializer(type));
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
      return allocate_term(this, RecursiveTerm::Initializer(global, result_type.get(), parameters));
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
      return allocate_term(this, ApplyTerm::Initializer(recursive.get(), parameters));
    }

    TermPtr<> ApplyTerm::unpack() const {
      throw std::logic_error("not implemented");
    }
  }
}
