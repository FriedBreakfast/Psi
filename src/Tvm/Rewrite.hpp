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
    ValuePtr<ApplyValue> rewrite_apply_term(T& rewriter, const ValuePtr<ApplyValue>& term) {
      ValuePtr<> recursive_base = rewriter(term->recursive());
      if (recursive_base->term_type() != term_recursive)
        throw TvmInternalError("result of rewriting recursive term was not a recursive term");
      ValuePtr<RecursiveType> recursive = value_cast<RecursiveType>(recursive_base);
      std::vector<ValuePtr<> > parameters;
      for (unsigned i = 0; i != parameters.size(); ++i)
        parameters.push_back(rewriter(term->parameter(i)));
      return term->context().apply_recursive(recursive, parameters);
    }

    /**
     * Rewrite a functional term, using a callback to rewrite each
     * parameter, and return a functional term over the rewritten
     * parameters. This does not allow changing the backend of the
     * apply term.
     */
    template<typename T>
    ValuePtr<FunctionalValue> rewrite_functional_term(T& rewriter, const ValuePtr<FunctionalValue>& term) {
      class MyRewriteCallback : public RewriteCallback {
        T *m_rewriter;
      public:
        MyRewriteCallback(Context *context, T *rewriter) : RewriteCallback(context) {}
        virtual ValuePtr<> rewrite(const ValuePtr<>& value) {return (*m_rewriter)(value);}
      };
      MyRewriteCallback my_rewrite_callback(&term->context(), &rewriter);
      return term->rewrite(my_rewrite_callback);
    }
    
    template<typename T>
    ValuePtr<FunctionType> rewrite_function_type_term(T& rewriter, const ValuePtr<FunctionType>& term) {
      std::vector<ValuePtr<FunctionTypeParameter> > parameters;
      for (unsigned i = 0, e = parameters.size(); i != e; ++i) {
        ValuePtr<> param_type = rewriter(term->parameter_types()[i]);
        parameters.push_back(term->context().new_function_type_parameter(param_type));
      }

      ValuePtr<> result_type = rewriter(term->result_type());
      
      return term->context().get_function_type
        (term->calling_convention(),
         result_type,
         parameters,
         term->n_phantom(),
         term->location());
    }

    /**
     * Rewrite a term in a default way, i.e. calling out to other
     * rewriter functions depending on the term type such as
     * #rewrite_apply_term and #rewrite_functional_term.
     */
    template<typename T>
    ValuePtr<> rewrite_term_default(T& rewriter, const ValuePtr<>& term) {
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
        return rewrite_apply_term(rewriter, value_cast<ApplyValue>(term));

      case term_functional:
        return rewrite_functional_term(rewriter, value_cast<FunctionalValue>(term));

      case term_function_type:
        return rewrite_function_type_term(rewriter, value_cast<FunctionType>(term));

      case term_function_type_parameter:
        throw TvmInternalError("unresolved function parameter encountered during term rewriting");

      default:
        throw TvmInternalError("unknown term type");
      }
    }
  }
}

#endif
