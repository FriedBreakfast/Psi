#ifndef HPP_PSI_MACROS
#define HPP_PSI_MACROS

#include "Compiler.hpp"
#include "Tree.hpp"

namespace Psi {
  namespace Compiler {
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> function_definition_macro(CompileContext&, const SourceLocation&);
    TreePtr<Term> class_definition_macro(CompileContext& compile_context, const SourceLocation& location);
  }
}

#endif
