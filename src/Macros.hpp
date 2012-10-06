#ifndef HPP_PSI_MACROS
#define HPP_PSI_MACROS

#include "Compiler.hpp"
#include "Tree.hpp"

namespace Psi {
  namespace Compiler {
    TreePtr<Term> none_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> namespace_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> function_definition_macro(CompileContext&, const SourceLocation&);
    TreePtr<Term> class_definition_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> builtin_type_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> builtin_function_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> builtin_value_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> c_function_macro(CompileContext& compile_context, const SourceLocation& location);

    TreePtr<Term> macro_define_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> implementation_define_macro(CompileContext& compile_context, const SourceLocation& location);
    
    TreePtr<Term> library_macro(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<Term> library_symbol_macro(CompileContext& compile_context, const SourceLocation& location);
  }
}

#endif
