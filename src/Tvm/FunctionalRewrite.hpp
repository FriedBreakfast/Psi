#ifndef HPP_PSI_TVM_FUNCTIONAL_REWRITE
#define HPP_PSI_TVM_FUNCTIONAL_REWRITE

#include <tr1/unordered_map>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

#include "Core.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * An alternative term rewriter which is only suitable for
     * functional terms, but in this common case is much more useful.
     *
     * \tparam DataType User-specified data to be passed to rewriting
     * callback functions.
     */
    template<typename DataType>
    class FunctionalTermRewriter {
    public:
      struct Callback {
	virtual Term* rewrite(FunctionalTermRewriter&, FunctionalTerm*) const = 0;
      };

      typedef std::tr1::unordered_map<const char*, boost::shared_ptr<Callback> > CallbackMap;

    private:
      class ParameterListRewriter : public ScopedTermPtrArray<> {
      public:
	template<typename U, typename V>
	ParameterListRewriter(U* term, FunctionalTermRewriter<DataType> *self)
	  : ScopedTermPtrArray<>(term->n_parameters()) {
	  for (std::size_t i = 0; i < this->size(); ++i)
	    (*this)[i] = self->rewrite(term->parameter(i), this->array().slice(0, i));
	}
      };

      template<typename TermTagType, typename RewriteCb>
      class CallbackImpl : public Callback {
	RewriteCb m_rewrite_cb;

      public:
	CallbackImpl(const RewriteCb& rewrite_cb) : m_rewrite_cb(rewrite_cb) {}

	virtual Term* rewrite(FunctionalTermRewriter& rewriter, FunctionalTerm *term) const {
	  return m_rewrite_cb(rewriter, cast<TermTagType>(term));
	}
      };

      /// Map of operations to handlers.
      const CallbackMap *m_callback_map;
      /// Associated context
      Context *m_context;
      /// User-specified data
      DataType m_data;

      typedef std::tr1::unordered_map<Term*, Term*> RewrittenTermMap;
      /// Map of terms which have already been rewritten, or are in
      /// the process of being rewritten.
      RewrittenTermMap m_rewritten_terms;

    public:
      class CallbackMapInitializer {
      public:
	CallbackMapInitializer() : m_next(0), m_operation(0) {}
	CallbackMapInitializer(const CallbackMapInitializer *next, const char *operation, const boost::shared_ptr<Callback>& callback)
	  : m_next(next), m_operation(operation), m_callback(callback) {
	}

	template<typename TermTagType, typename RewriteCb>
	CallbackMapInitializer add(RewriteCb rewrite_cb) {
	  return CallbackMapInitializer(this, TermTagType::operation, boost::make_shared<CallbackImpl<TermTagType, RewriteCb> >(rewrite_cb));
	}

	operator CallbackMap () {
	  CallbackMap callback_map;
	  for (const CallbackMapInitializer *initializer = this; initializer->m_next; initializer = initializer->m_next)
	    callback_map.insert(std::make_pair(initializer->m_operation, initializer->m_callback));
	  return callback_map;
	}

      private:
	const CallbackMapInitializer *m_next;
	const char *m_operation;
	boost::shared_ptr<Callback> m_callback;
      };

      /**
       * Return a type which can be used to initialize a callback map.
       */
      static CallbackMapInitializer callback_map_initializer() {
	return CallbackMapInitializer();
      }

      FunctionalTermRewriter(Context *context, const CallbackMap *callback_map, const DataType& data=DataType())
	: m_context(context), m_callback_map(callback_map), m_data(data) {
      }

      /// \brief Get the context which this rewriter is associated with
      Context& context() {return *m_context;}
      /// \brief Get the user-specified data
      DataType& data() {return m_data;}

      /**
       * Rewrite a term.
       *
       * Non-functional terms are not altered except for ApplyTerm
       * instances whose parameters (excluding the RecursiveTerm) are
       * rewritten.
       */
      Term* rewrite(Term *term) {
	if ((term->term_type() == term_functional) || (term->term_type() == term_apply)) {
	  typename RewrittenTermMap::iterator rewritten_map_it = m_rewritten_terms.find(term);
	  if (rewritten_map_it != m_rewritten_terms.end()) {
	    PSI_ASSERT_MSG(rewritten_map_it->second(), "self-referential term encountered during term rewriting");
	    return rewritten_map_it->second;
	  }

#ifdef PSI_DEBUG
	  // Create this slot and set it to NULL so recursive terms
	  // can be spotted.
	  m_rewritten_terms[term] = NULL;
#endif

	  Term *result;
	  if (term->term_type() == term_functional) {
	    FunctionalTerm *functional_term = cast<FunctionalTerm>(term);
	    typename CallbackMap::const_iterator callback_map_it = m_callback_map->find(functional_term->operation());
	    if (callback_map_it != m_callback_map->end()) {
	      result = callback_map_it->second->rewrite(*this, functional_term);
	    } else {
	      ParameterListRewriter parameters(functional_term, this);
	      result = functional_term->rewrite(parameters.array());
	    }
	  } else {
	    PSI_ASSERT(term->term_type() == term_apply);
	    ApplyTerm *apply_term = cast<ApplyTerm>(term);
	    ParameterListRewriter parameters(apply_term, this);
	    result = context().apply_recursive(apply_term->recursive(), parameters.array());
	  }

	  m_rewritten_terms.insert(std::make_pair(term, result));
	} else {
	  return term;
	}
      }
    };
  }
}

#endif
