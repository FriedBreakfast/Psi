#ifndef HPP_PSI_TVM_UTILITY
#define HPP_PSI_TVM_UTILITY

#include <algorithm>

#include <boost/type_traits/alignment_of.hpp>

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    inline bool term_abstract(const Term *t) {
      return t && t->abstract();
    }

    inline bool term_parameterized(const Term *t) {
      return t && t->parameterized();
    }

    inline bool term_global(const Term *t) {
      return !t || t->global();
    }

    inline Term* term_source(const Term *t) {
      return t ? t->source() : NULL;
    }

    template<typename T>
    bool any_abstract(ArrayPtr<T*const> t) {
      return std::find_if(t.get(), t.get()+t.size(), term_abstract) != (t.get()+t.size());
    }

    template<typename T>
    bool any_parameterized(ArrayPtr<T*const> t) {
      return std::find_if(t.get(), t.get()+t.size(), term_parameterized) != (t.get()+t.size());
    }

    template<typename T>
    Term* common_source(ArrayPtr<T*const> t) {
      Term *bl = NULL;
      for (std::size_t i = 0; i < t.size(); ++i)
        bl = common_source(bl, term_source(t[i]));
      return bl;
    }

    Term* common_source(Term *t1, Term *t2);
    bool source_dominated(Term *dominator, Term *dominated);

    /**
     * \brief Compute the offset to the next field.
     *
     * \param base Offset to the current field
     * \param size Size of the current field
     * \param align Alignment of the next field
     */
    inline std::size_t struct_offset(std::size_t base, std::size_t size, std::size_t align) {
      return (base + size + align - 1) & ~(align - 1);
    }

    /**
     * \brief Offset a pointer by a specified number of bytes.
     */
    inline void* ptr_offset(void *p, std::size_t offset) {
      return static_cast<void*>(static_cast<char*>(p) + offset);
    }

    /**
     * \brief Base class for initializers passed to #allocate_term.
     */
    template<typename T>
    struct InitializerBase {
      typedef T TermType;

      std::size_t term_size() const {
	return sizeof(T);
      }
    };

    /**
     * \brief Allocate a term.
     */
    template<typename T>
    typename T::TermType* Context::allocate_term(const T& initializer) {
      std::size_t n_uses = initializer.n_uses();

      std::size_t use_offset = struct_offset(0, initializer.term_size(), boost::alignment_of<Use>::value);
      std::size_t total_size = use_offset + sizeof(Use)*(n_uses+2);

      void *term_base = operator new (total_size);
      Use *uses = static_cast<Use*>(ptr_offset(term_base, use_offset));
      try {
	typename T::TermType* t(initializer.initialize(term_base, UserInitializer(n_uses+1, uses), this));
        m_all_terms.push_back(*t);
        return t;
      } catch(...) {
	operator delete (term_base);
	throw;
      }
    }

    template<typename T>
    struct SetupHasher {
      std::size_t operator () (const T& key) const {
	return key.hash();
      }
    };

    template<typename T>
    struct SetupEquals {
      std::size_t operator () (const T& key, const HashTerm& value) const {
	return key.equals(const_cast<HashTerm*>(&value));
      }
    };

    /**
     * \brief Create (or get an existing) hashable term.
     */
    template<typename T>
    typename T::TermType* Context::hash_term_get(T& setup) {
      typename HashTermSetType::insert_commit_data commit_data;
      std::pair<typename HashTermSetType::iterator, bool> existing =
	m_hash_terms.insert_check(setup, SetupHasher<T>(), SetupEquals<T>(), commit_data);
      if (!existing.second)
	return cast<typename T::TermType>(&*existing.first);

      setup.prepare_initialize(this);
      typename T::TermType* term = allocate_term(setup);
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
  }
}

#endif
