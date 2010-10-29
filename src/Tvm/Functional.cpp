#include "Functional.hpp"
#include "Utility.hpp"

namespace {
  std::size_t hash_type_info(const std::type_info& ti) {
#if __GXX_MERGED_TYPEINFO_NAMES
    return boost::hash_value(ti.name());
#else
    std::size_t h = 0;
    for (const char *p = ti.name(); *p != '\0'; ++p)
      boost::hash_combine(h, *p);
    return h;
#endif
  }
}

namespace Psi {
  namespace Tvm {
    FunctionalTermBackend::~FunctionalTermBackend() {
    }

    std::size_t FunctionalTermBackend::hash_value() const {
      std::size_t h = 0;
      boost::hash_combine(h, hash_internal());
      boost::hash_combine(h, hash_type_info(typeid(*this)));
      return h;
    }

    FunctionalTerm::FunctionalTerm(const UserInitializer& ui, Context *context, TermRef<> type,
				   std::size_t hash, FunctionalTermBackend *backend,
				   std::size_t n_parameters, Term *const* parameters)
      : HashTerm(ui, context, term_functional,
		 term_abstract(type.get()) || any_abstract(n_parameters, parameters),
		 term_parameterized(type.get()) || any_parameterized(n_parameters, parameters),
		 term_global(type.get()) && all_global(n_parameters, parameters),
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

      Setup(std::size_t n_parameters, Term *const* parameters, const FunctionalTermBackend *backend, Term *type)
	: m_n_parameters(n_parameters),
	  m_parameters(parameters),
	  m_backend(backend),
	  m_type(type) {

	m_hash = 0;
	boost::hash_combine(m_hash, backend->hash_value());
	for (std::size_t i = 0; i < n_parameters; ++i)
	  boost::hash_combine(m_hash, parameters[i]->hash_value());
      }

      void prepare_initialize(Context *context) {
	if (!m_type) {
	  m_type_ptr = m_backend->type(*context, m_n_parameters, m_parameters);
	  m_type = m_type_ptr.get();
	}

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

      bool equals(const HashTerm& term) const {
	if ((m_hash != term.m_hash) || (term.term_type() != term_functional))
	  return false;

	const FunctionalTerm& cast_term = checked_cast<const FunctionalTerm&>(term);

	if (m_n_parameters != cast_term.n_parameters())
	  return false;

	for (std::size_t i = 0; i < m_n_parameters; ++i) {
	  if (m_parameters[i] != cast_term.parameter(i).get())
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
      std::size_t m_n_parameters;
      Term *const* m_parameters;
      const FunctionalTermBackend *m_backend;
      Term *m_type;
      TermPtr<> m_type_ptr;
    };

    TermPtr<FunctionalTerm> Context::get_functional_internal(const FunctionalTermBackend& backend, std::size_t n_parameters, Term *const* parameters) {
      FunctionalTerm::Setup setup(n_parameters, parameters, &backend, NULL);
      return hash_term_get(setup);
    }

    TermPtr<FunctionalTerm> Context::get_functional_internal_with_type(const FunctionalTermBackend& backend, TermRef<> type, std::size_t n_parameters, Term *const* parameters) {
      FunctionalTerm::Setup setup(n_parameters, parameters, &backend, type.get());
      return hash_term_get(setup);
    }
  }
}
