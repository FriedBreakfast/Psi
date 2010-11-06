#include "Rewrite.hpp"


namespace Psi {
  namespace Tvm {
    TermPtr<ApplyTerm> rewrite_apply_term(TermRef<ApplyTerm> term) const {
      TermPtr<RecursiveTerm> recursive = dynamic_term_cast<RecursiveTerm>(rewrite(term->recursive()));
      if (!recursive)
	throw std::logic_error("result of rewriting recursive term was not a recursive term");
      ParameterListRewriter parameters(term, ParameterListCallback(this));
      return term->context().apply_recursive(recursive, parameters.n(), parameters.get());
    }
  }
}
