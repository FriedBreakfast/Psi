#include "Functional.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Tvm {
    FunctionalTerm::FunctionalTerm(const UserInitializer& ui, Context *context, Term* type,
				   bool phantom, std::size_t hash, FunctionalTermBackend *backend,
				   ArrayPtr<Term*const> parameters)
      : HashTerm(ui, context, term_functional,
		 term_abstract(type) || any_abstract(parameters),
		 term_parameterized(type) || any_parameterized(parameters),
                 phantom,
                 common_source(term_source(type), common_source(parameters)),
		 type, hash),
	m_backend(backend) {
      for (std::size_t i = 0; i < parameters.size(); ++i)
	set_base_parameter(i, parameters[i]);
    }

    FunctionalTerm::~FunctionalTerm() {
      m_backend->~FunctionalTermBackend();
    }

    class FunctionalTerm::Setup {
    public:
      typedef FunctionalTerm TermType;

      Setup(ArrayPtr<Term*const> parameters, const FunctionalTermBackend *backend)
	: m_parameters(parameters),
	  m_backend(backend) {

	m_hash = 0;
	boost::hash_combine(m_hash, backend->hash_value());
	for (std::size_t i = 0; i < parameters.size(); ++i)
	  boost::hash_combine(m_hash, parameters[i]->hash_value());
      }

      void prepare_initialize(Context *context) {
        FunctionalTypeResult tr = m_backend->type(*context, m_parameters);
        m_type = tr.type;
        m_phantom = tr.phantom;

	std::pair<std::size_t, std::size_t> backend_size_align = m_backend->size_align();
	PSI_ASSERT_MSG((backend_size_align.second & (backend_size_align.second - 1)) == 0, "alignment is not a power of two");
	m_proto_offset = struct_offset(0, sizeof(FunctionalTerm), backend_size_align.second);
	m_size = m_proto_offset + backend_size_align.first;
      }

      FunctionalTerm* initialize(void *base, const UserInitializer& ui, Context *context) const {
	FunctionalTermBackend *new_backend = m_backend->clone(ptr_offset(base, m_proto_offset));
	try {
	  return new (base) FunctionalTerm(ui, context, m_type, m_phantom, m_hash, new_backend, m_parameters);
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
	return m_parameters.size();
      }

      bool equals(const HashTerm& term) const {
	if ((m_hash != term.m_hash) || (term.term_type() != term_functional))
	  return false;

	const FunctionalTerm& cast_term = checked_cast<const FunctionalTerm&>(term);

	if (m_parameters.size() != cast_term.n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_parameters.size(); ++i) {
	  if (m_parameters[i] != cast_term.parameter(i))
	    return false;
	}

	if ((typeid(*m_backend) != typeid(*cast_term.m_backend))
	    || !m_backend->equals(*cast_term.m_backend))
	  return false;

	return true;
      }

    private:
      std::size_t m_proto_offset;
      std::size_t m_size;
      std::size_t m_hash;
      ArrayPtr<Term*const> m_parameters;
      const FunctionalTermBackend *m_backend;
      Term* m_type;
      bool m_phantom;
    };

    /**
     * Get a functional term by directly passing the
     * FunctionalTermBackend interface and getting back a TermPtr,
     * rather than the more friendly FunctionalTermPtr.
     */
    FunctionalTerm* Context::get_functional_bare(const FunctionalTermBackend& backend, ArrayPtr<Term*const> parameters) {
      FunctionalTerm::Setup setup(parameters, &backend);
      return hash_term_get(setup);
    }
  }
}
