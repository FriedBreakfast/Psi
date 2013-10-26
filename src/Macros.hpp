#ifndef HPP_PSI_MACROS
#define HPP_PSI_MACROS

#include "Compiler.hpp"
#include "Tree.hpp"
#include "Export.hpp"

namespace Psi {
  namespace Compiler {
    TreePtr<> default_macro_term(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<> default_type_macro_term(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<> default_macro_member(CompileContext& compile_context, const SourceLocation& location);
    TreePtr<> default_type_macro_member(CompileContext& compile_context, const SourceLocation& location);
    
    PSI_COMPILER_EXPORT TreePtr<Term> lifecycle_init_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> lifecycle_fini_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> lifecycle_move_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> lifecycle_copy_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> lifecycle_no_move_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> lifecycle_no_copy_macro(CompileContext& compile_context, const SourceLocation& location);
    
    PSI_COMPILER_EXPORT TreePtr<Term> new_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> namespace_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> number_value_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> pointer_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> function_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> struct_macro(CompileContext& compile_context, const SourceLocation& location);

    PSI_COMPILER_EXPORT TreePtr<Term> macro_define_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location);
    
    PSI_COMPILER_EXPORT TreePtr<Term> library_macro(CompileContext& compile_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Term> string_macro(CompileContext& compile_context, const SourceLocation& location);

    struct ConstantMetadataSetup {
      TreePtr<MetadataType> type;
      TreePtr<> value;
      unsigned n_wildcards;
      /// Extra pattern variables
      PSI_STD::vector<TreePtr<Term> > pattern;
      
      template<typename V>
      static void visit(V& v) {
        v("type", &ConstantMetadataSetup::type)
        ("value", &ConstantMetadataSetup::value)
        ("n_wildcards", &ConstantMetadataSetup::n_wildcards)
        ("pattern", &ConstantMetadataSetup::pattern);
      }
    };

    TreePtr<Term> make_annotated_type(CompileContext& compile_context, const PSI_STD::vector<ConstantMetadataSetup>& metadata, const SourceLocation& location);
    TreePtr<Term> make_macro_tag_term(const TreePtr<Macro>& macro, const TreePtr<Term>& tag, const SourceLocation& location);
    TreePtr<Term> make_macro_term(const TreePtr<Macro>& macro, const SourceLocation& location);

    class MacroMemberCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroMemberCallbackVtable {
      TreeVtable base;
      void (*evaluate) (TreePtr<Term>*, const MacroMemberCallback*, const TreePtr<Term>*, const PSI_STD::vector<SharedPtr<Parser::Expression> >*, const TreePtr<EvaluateContext>*, const SourceLocation*);
    };

    class MacroMemberCallback : public Tree {
    public:
      typedef MacroMemberCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroMemberCallback(const MacroMemberCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value, const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) const {
        ResultStorage<TreePtr<Term> > rs;
        derived_vptr(this)->evaluate(rs.ptr(), this, &value, &parameters, &evaluate_context, &location);
        return rs.done();
      }
    };

    template<typename Derived>
    struct MacroMemberCallbackWrapper : NonConstructible {
      static void evaluate(TreePtr<Term> *out, const MacroMemberCallback *self, const TreePtr<Term> *value, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
        new (out) TreePtr<Term> (Derived::evaluate_impl(*static_cast<const Derived*>(self), *value, *parameters, *evaluate_context, *location));
      }
    };

#define PSI_COMPILER_MACRO_MEMBER_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroMemberCallbackWrapper<derived>::evaluate \
  }

    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroMemberCallback>&, const std::map<String, TreePtr<MacroMemberCallback> >&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroMemberCallback>&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<MacroMemberCallback> >&);
  }
}

#endif
