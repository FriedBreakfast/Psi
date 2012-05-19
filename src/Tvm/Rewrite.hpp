#ifndef HPP_PSI_TVM_REWRITE
#define HPP_PSI_TVM_REWRITE

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Recursive.hpp"

namespace Psi {
  namespace Tvm {
    /**
     * Rewrite an apply term, using a callback to rewrite each
     * parameter, and return an apply term over the rewritten
     * parameters. This allows rewriting of the recursive term used.
     */
    template<typename T>
    ApplyTerm* rewrite_apply_term(T& rewriter, ApplyTerm* term) {
      Term* recursive_base = rewriter(term->recursive());
      if (recursive_base->term_type() != term_recursive)
        throw TvmInternalError("result of rewriting recursive term was not a recursive term");
      RecursiveTerm* recursive = cast<RecursiveTerm>(recursive_base);
      ScopedArray<Term*> parameters(term->n_parameters());
      for (unsigned i = 0; i != parameters.size(); ++i)
        parameters[i] = rewriter(term->parameter(i));
      return term->context().apply_recursive(recursive, parameters);
    }

    /**
     * Rewrite a functional term, using a callback to rewrite each
     * parameter, and return a functional term over the rewritten
     * parameters. This does not allow changing the backend of the
     * apply term.
     */
    template<typename T>
    FunctionalTerm* rewrite_functional_term(T& rewriter, FunctionalTerm* term) {
      ScopedArray<Term*> parameters(term->n_parameters());
      for (unsigned i = 0; i != parameters.size(); ++i)
        parameters[i] = rewriter(term->parameter(i));
      return term->rewrite(term->context(), parameters);
    }
    
    template<typename T>
    FunctionTypeTerm* rewrite_function_type_term(T& rewriter, FunctionTypeTerm *term) {
      ScopedArray<FunctionTypeParameterTerm*> parameters(term->n_parameters());
      for (unsigned i = 0, e = parameters.size(); i != e; ++i) {
        Term *param_type = rewriter(term->parameter_type(i));
        parameters[i] = term->context().new_function_type_parameter(param_type);
      }

      Term* result_type = rewriter(term->result_type());
      
      std::size_t n_phantom = term->n_phantom_parameters();
      std::size_t n_parameters = term->n_parameters();

      return term->context().get_function_type
        (term->calling_convention(),
         result_type,
         parameters.slice(0, n_phantom),
         parameters.slice(n_phantom, n_parameters - n_phantom));
    }

    /**
     * Rewrite a term in a default way, i.e. calling out to other
     * rewriter functions depending on the term type such as
     * #rewrite_apply_term and #rewrite_functional_term.
     */
    template<typename T>
    Term* rewrite_term_default(T& rewriter, Term* term) {
      if (term_unique(term))
        return term;

      switch (term->term_type()) {
      case term_recursive:
        throw TvmInternalError("cannot rewrite recursive terms since "
                               "they cannot be compared for structural identity");

      case term_recursive_parameter:
        throw TvmInternalError("cannot rewrite recursive parameter "
                               "since these should only occur inside "
                               "a recursive term (which cannot be rewritten)");

      case term_apply:
        return rewrite_apply_term(rewriter, cast<ApplyTerm>(term));

      case term_functional:
        return rewrite_functional_term(rewriter, cast<FunctionalTerm>(term));

      case term_function_type:
        return rewrite_function_type_term(rewriter, cast<FunctionTypeTerm>(term));

      case term_function_type_parameter:
        throw TvmInternalError("unresolved function parameter encountered during term rewriting");

      default:
        throw TvmInternalError("unknown term type");
      }
    }
  }
}

#endif
