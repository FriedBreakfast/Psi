#ifndef HPP_PSI_TVM_REWRITE
#define HPP_PSI_TVM_REWRITE

#include <stdexcept>

#include "Core.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    template<typename T=Term>
    class ParameterListRewriter : public ScopedTermPtrArray<T> {
    public:
      template<typename U, typename V>
      ParameterListRewriter(U* term, const V& rewriter)
	: ScopedTermPtrArray<T>(term->n_parameters()) {
	for (std::size_t i = 0; i < this->size(); ++i)
          (*this)[i] = rewriter(term->parameter(i), this->array().slice(0, i));
      }
    };

    template<typename T>
    class ParameterListRewriterAdapter {
    public:
      ParameterListRewriterAdapter(const T *self) : m_self(self) {}

      Term* operator () (Term* term, ArrayPtr<Term*const>) const {
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
    ApplyTerm* rewrite_apply_term(const T& rewriter, ApplyTerm* term) {
      Term* recursive_base = rewriter(term->recursive());
      if (recursive_base->term_type() != term_recursive)
	throw std::logic_error("result of rewriting recursive term was not a recursive term");
      RecursiveTerm* recursive = checked_cast<RecursiveTerm*>(recursive_base);
      ParameterListRewriter<> parameters(term, ParameterListRewriterAdapter<T>(&rewriter));
      return term->context().apply_recursive(recursive, parameters.array());
    }

    /**
     * Rewrite a functional term, using a callback to rewrite each
     * parameter, and return a functional term over the rewritten
     * parameters. This does not allow changing the backend of the
     * apply term.
     */
    template<typename T>
    FunctionalTerm* rewrite_functional_term(const T& rewriter, FunctionalTerm* term) {
      ParameterListRewriter<> parameters(term, ParameterListRewriterAdapter<T>(&rewriter));
      return term->context().get_functional_bare(*term->backend(), parameters.array());
    }

    /**
     * Rewrite a term in a default way, i.e. calling out to other
     * rewriter functions depending on the term type such as
     * #rewrite_apply_term and #rewrite_functional_term.
     */
    template<typename T>
    Term* rewrite_term_default(const T& rewriter, Term* term) {
      if (term_unique(term))
        return term;

      switch (term->term_type()) {
      case term_recursive:
	throw std::logic_error("cannot rewrite recursive terms since "
			       "they cannot be compared for structural identity");

      case term_recursive_parameter:
	throw std::logic_error("cannot rewrite recursive parameter "
			       "since these should only occur inside "
			       "a recursive term (which cannot be rewritten)");

      case term_apply:
	return rewrite_apply_term(rewriter, checked_cast<ApplyTerm*>(term));

      case term_functional:
	return rewrite_functional_term(rewriter, checked_cast<FunctionalTerm*>(term));

      case term_function_type:
        throw std::logic_error("function type term rewriting must happen through TermRewriter");

      case term_function_type_parameter:
	throw std::logic_error("cannot rewrite a function type parameter term in a "
                               "default way since it must be mapped to a value "
                               "from rewriting earlier terms");

      case term_function_type_resolver:
	throw std::logic_error("function type resolver terms should not be passed "
                               "to rewrite_term_default");

      default:
	throw std::logic_error("unknown term type");
      }
    }

    /**
     * Term rewriter class: this must be used if support for
     * rewriting function terms is required.
     */
    template<typename T>
    class TermRewriter {
      typedef std::tr1::unordered_map<FunctionTypeTerm*, ArrayPtr<FunctionTypeParameterTerm*const> > FunctionMapType;
 
      class FunctionTypeParameterListCallback {
      public:
        FunctionTypeParameterListCallback(T *callback, ArrayPtr<FunctionTypeParameterTerm*const> *status)
          : m_callback(callback), m_status(status) {}

        FunctionTypeParameterTerm* operator () (Term* term, ArrayPtr<FunctionTypeParameterTerm*const> previous) const {
          *m_status = previous;
          FunctionTypeParameterTerm *param = checked_cast<FunctionTypeParameterTerm*>(term);	  
          Term* type = (*m_callback)(param->type());
          return term->context().new_function_type_parameter(type);
        }

      private:
        T *m_callback;
        ArrayPtr<FunctionTypeParameterTerm*const> *m_status;
      };

    public:
      explicit TermRewriter(T *user) : m_user(user) {}

      Term* operator () (Term* term) {
        switch (term->term_type()) {
        case term_function_type: {
          FunctionTypeTerm *cast_term = checked_cast<FunctionTypeTerm*>(term);
          PSI_ASSERT_MSG(m_functions.find(cast_term) == m_functions.end(),
                         "recursive function types not supported");
          ArrayPtr<FunctionTypeParameterTerm*const>& status = m_functions[cast_term];
          ParameterListRewriter<FunctionTypeParameterTerm> parameters(cast_term,
                                                                      FunctionTypeParameterListCallback(m_user, &status));
          Term* result_type = (*m_user)(cast_term->result_type());
          m_functions.erase(cast_term);

          std::size_t n_phantom = cast_term->n_phantom_parameters();
          std::size_t n_parameters = cast_term->n_parameters();

          return term->context().get_function_type
            (cast_term->calling_convention(),
             result_type,
             parameters.array().slice(0, n_phantom),
             parameters.array().slice(n_phantom, n_parameters - n_phantom));
        }

        case term_function_type_parameter: {
          FunctionTypeParameterTerm *cast_term = checked_cast<FunctionTypeParameterTerm*>(term);
          FunctionTypeTerm* source = cast_term->source();

          FunctionMapType::iterator it = m_functions.find(source);
          if (it == m_functions.end())
            throw std::logic_error("encountered parameter to unknown function during term rewriting");

          if (cast_term->index() >= it->second.size())
            throw std::logic_error("function type parameter definition refers to value of later parameter");

          return it->second[cast_term->index()];
        }

        default:
          return rewrite_term_default(*m_user, term);
        }
      }

    private:
      TermRewriter(const TermRewriter&);
      FunctionMapType m_functions;
      T *m_user;
    };
  }
}

#endif
