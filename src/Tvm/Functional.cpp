#include "Functional.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Tvm {
    FunctionalTerm::FunctionalTerm(const UserInitializer& ui, Context *context, Term* type, Term *source,
				   std::size_t hash, const char *operation,
				   ArrayPtr<Term*const> parameters)
      : HashTerm(ui, context, term_functional, source, type, hash),
	m_operation(operation) {
      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i, parameters[i]);
    }

    class FunctionalTerm::Setup {
    public:
      typedef FunctionalTerm TermType;

      Setup(ArrayPtr<Term*const> parameters, const FunctionalTermSetup *setup)
	: m_parameters(parameters),
	  m_setup(setup) {

	m_hash = setup->data_hash;
        boost::hash_combine(m_hash, setup->operation);
	for (std::size_t i = 0; i < parameters.size(); ++i)
	  boost::hash_combine(m_hash, parameters[i]->hash_value());
      }

      void prepare_initialize(Context *context) {
        FunctionalTypeResult tr = m_setup->type(*context, m_parameters);
        m_type = tr.type;
        m_source = tr.source_set ? tr.source : common_source(m_parameters);
      }

      FunctionalTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
        return m_setup->construct(base, ui, context, m_type, m_source, m_hash, m_setup->operation, m_parameters);
      }

      std::size_t hash() const {
	return m_hash;
      }

      std::size_t term_size() const {
        return m_setup->term_size;
      }

      std::size_t n_uses() const {
	return m_parameters.size();
      }

      bool equals(HashTerm *term) const {
	if ((m_hash != term->m_hash) || (term->term_type() != term_functional))
	  return false;

	FunctionalTerm *cast_term = cast<FunctionalTerm>(term);

	if (m_parameters.size() != cast_term->n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_parameters.size(); ++i) {
	  if (m_parameters[i] != cast_term->parameter(i))
	    return false;
	}

        if (m_setup->operation != cast_term->operation())
          return false;

        if (!m_setup->data_equals(cast_term))
          return false;

	return true;
      }

    private:
      std::size_t m_hash;
      ArrayPtr<Term*const> m_parameters;
      const FunctionalTermSetup *m_setup;
      Term *m_type, *m_source;
    };

    /**
     * Get a functional term by directly passing the
     * FunctionalTermBackend interface and getting back a TermPtr,
     * rather than the more friendly FunctionalTermPtr.
     */
    FunctionalTerm* Context::get_functional_bare(const FunctionalTermSetup& setup, ArrayPtr<Term*const> parameters) {
      FunctionalTerm::Setup func_setup(parameters, &setup);
      return hash_term_get(func_setup);
    }
  }
}
