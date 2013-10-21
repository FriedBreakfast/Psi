#ifndef HPP_PSI_MACROS
#define HPP_PSI_MACROS

#include "Compiler.hpp"
#include "Tree.hpp"
#include "Export.hpp"

namespace Psi {
  namespace Compiler {
    PSI_COMPILER_EXPORT TreePtr<> default_macro_impl(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<> default_type_macro_impl(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> new_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> namespace_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> number_type_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> builtin_function_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> number_value_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> pointer_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> function_definition_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> function_type_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> struct_macro(CompileContext& compile_context, const SourceLocation& location);

    PSI_COMPILER_EXPORT TreePtr<Term> macro_define_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> implementation_define_macro(CompileContext& compile_context, const SourceLocation& location);
    
    PSI_COMPILER_EXPORT TreePtr<Term> library_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> string_macro(CompileContext& compile_context, const SourceLocation& location);
  }
}

#endif
