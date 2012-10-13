#include "Compiler.hpp"
#include "Interface.hpp"

namespace Psi {
  namespace Compiler {
    class MacroDefineMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      MacroDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const MacroDefineMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroMemberCallbackVtable MacroDefineMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(MacroDefineMacro, "psi.compiler.MacroDefineMacro", MacroMemberCallback);
    
    /**
     * Return a term which is a macro for defining new macros.
     */
    TreePtr<Term> macro_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new MacroDefineMacro(compile_context, location)));
      return make_macro_term(m, location);
    }
    
    /**
     * Create a new interface.
     */
    class InterfaceDefineMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      InterfaceDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const InterfaceDefineMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroMemberCallbackVtable InterfaceDefineMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(InterfaceDefineMacro, "psi.compiler.InterfaceDefineMacro", MacroMemberCallback);

    /**
     * Return a term which is a macro for defining new interfaces.
     */
    TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new InterfaceDefineMacro(compile_context, location)));
      return make_macro_term(m, location);
    }

    /**
     * Define a new macro.
     */
    class ImplementationDefineMacro : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      ImplementationDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const ImplementationDefineMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroMemberCallbackVtable ImplementationDefineMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(ImplementationDefineMacro, "psi.compiler.ImplementationDefineMacro", MacroMemberCallback);

    /**
     * Return a term which is a macro for creating interface implementations.
     */
    TreePtr<Term> implementation_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroMemberCallback>(new ImplementationDefineMacro(compile_context, location)));
      return make_macro_term(m, location);
    }

    const SIVtable TypeConstructorInfoCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.TypeConstructorInfo", Tree);
  }
}
