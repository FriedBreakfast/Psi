#include "Compiler.hpp"
#include "Interface.hpp"

namespace Psi {
  namespace Compiler {
    class MacroDefineMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      MacroDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const MacroDefineMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroEvaluateCallbackVtable MacroDefineMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(MacroDefineMacro, "psi.compiler.MacroDefineMacro", MacroEvaluateCallback);
    
    /**
     * Return a term which is a macro for defining new macros.
     */
    TreePtr<Term> macro_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new MacroDefineMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }
    
    /**
     * Create a new interface.
     */
    class InterfaceDefineMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      InterfaceDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const InterfaceDefineMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroEvaluateCallbackVtable InterfaceDefineMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(InterfaceDefineMacro, "psi.compiler.InterfaceDefineMacro", MacroEvaluateCallback);

    /**
     * Return a term which is a macro for defining new interfaces.
     */
    TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new InterfaceDefineMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }

    /**
     * Define a new macro.
     */
    class ImplementationDefineMacro : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      ImplementationDefineMacro(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const ImplementationDefineMacro& self,
                                         const TreePtr<Term>& value,
                                         const List<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroEvaluateCallbackVtable ImplementationDefineMacro::vtable = PSI_COMPILER_MACRO_EVALUATE_CALLBACK(ImplementationDefineMacro, "psi.compiler.ImplementationDefineMacro", MacroEvaluateCallback);

    /**
     * Return a term which is a macro for creating interface implementations.
     */
    TreePtr<Term> implementation_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Macro> m = make_macro(compile_context, location, TreePtr<MacroEvaluateCallback>(new ImplementationDefineMacro(compile_context, location)));
      return make_macro_term(compile_context, location, m);
    }

    const SIVtable TypeConstructorInfoCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.TypeConstructorInfo", Tree);
  }
}
