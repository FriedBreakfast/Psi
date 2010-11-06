#ifndef HPP_PSI_TVM_REWRITE
#define HPP_PSI_TVM_REWRITE

#include <stdexcept>

#include "Core.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    class ParameterListRewriter : public TermPtrArray<> {
    public:
      template<typename T, typename U>
      ParameterListRewriter(TermRef<T> term, const U& rewriter)
	: TermPtrArray<>(term->n_parameters()) {
	for (std::size_t i = 0; i < size(); ++i)
	  set(i, rewriter(term->parameter(i), i));
      }
    };

    template<typename T>
    class ParameterListRewriterAdapter {
    public:
      ParameterListRewriterAdapter(const T *self) : m_self(self) {}

      TermPtr<> operator () (TermRef<> term, std::size_t) const {
	return (*m_self)(term);
      }

    private:
      const T *m_self;
    };

    /**
     * Rewrite an apply term, using a callback to rewrite each
     * parameter, and return an apply term over the rewritten
     * parameters. This allows rewriting of the recursive term used.
     */
    template<typename T>
    TermPtr<ApplyTerm> rewrite_apply_term(const T& rewriter, TermRef<ApplyTerm> term) {
      TermPtr<RecursiveTerm> recursive = dynamic_term_cast<RecursiveTerm>(rewriter(term->recursive()));
      if (!recursive)
	throw std::logic_error("result of rewriting recursive term was not a recursive term");
      ParameterListRewriter parameters(term, ParameterListRewriterAdapter<T>(&rewriter));
      return term->context().apply_recursive(recursive, parameters);
    }

    /**
     * Rewrite a functional term, using a callback to rewrite each
     * parameter, and return a functional term over the rewritten
     * parameters. This does not allow changing the backend of the
     * apply term.
     */
    template<typename T>
    TermPtr<FunctionalTerm> rewrite_functional_term(const T& rewriter, TermRef<FunctionalTerm> term) {
      ParameterListRewriter parameters(term, ParameterListRewriterAdapter<T>(&rewriter));
      return term->context().get_functional_bare(*term->backend(), parameters);
    }

    /**
     * Rewrite a term in a default way, i.e. calling out to other
     * rewriter functions depending on the term type such as
     * #rewrite_apply_term and #rewrite_functional_term.
     */
    template<typename T>
    TermPtr<> rewrite_term_default(const T& rewriter, TermRef<> term) {
      if (term_unique(term))
	throw std::logic_error("cannot rewrite unique term");

      switch (term->term_type()) {
      case term_recursive:
	throw std::logic_error("cannot rewrite recursive terms since "
			       "they cannot be compared for structural identity");

      case term_recursive_parameter:
	throw std::logic_error("cannot rewrite recursive parameter "
			       "since these should only occur inside "
			       "a recursive term (which cannot be rewritten)");

      case term_apply:
	return rewrite_apply_term(rewriter, checked_cast<ApplyTerm*>(term.get()));

      case term_functional:
	return rewrite_functional_term(rewriter, checked_cast<FunctionalTerm*>(term.get()));

      case term_function_type:
      case term_function_type_parameter:
      case term_function_type_resolver:
	throw std::logic_error("not implemented");

      default:
	throw std::logic_error("unknown term type");
      }
    }
  }
}

#endif
