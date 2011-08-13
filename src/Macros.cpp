#include "Macros.hpp"

namespace Psi {
  namespace Compiler {
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> macro = make_macro(compile_context, location);      
      TreePtr<Term> term = make_macro_term(compile_context, location);
      attach_compile_implementation(compile_context.macro_interface(), treeptr_cast<ImplementationTerm>(term->type()), macro, location);
      return term;
    }
  }
}
