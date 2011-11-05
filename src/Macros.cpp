#include "Macros.hpp"

namespace Psi {
  namespace Compiler {
    TreePtr<Term> make_macro_term(CompileContext& compile_context,
                                  const SourceLocation& location,
                                  const TreePtr<Macro>& macro) {
      TreePtr<Implementation> impl(new Implementation(compile_context, macro, default_, default_, location));
      PSI_STD::vector<TreePtr<Implementation> > implementations(1, impl);
      return TreePtr<Term>(new ImplementationTerm(compile_context.metatype(), implementations, location));
    }
    
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location) {
    }
  }
}
