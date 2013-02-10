#include "Compiler.hpp"
#include "Interface.hpp"
#include "Tree.hpp"

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
    
    class InterfaceTypeHelper {
      TreePtr<GenericType> m_generic;
      PSI_STD::vector<TreePtr<Term> > m_introduced_parameters;
      PSI_STD::vector<TreePtr<Term> > m_generic_parameters;
      
    public:
      typedef Term TreeResultType;
      
      InterfaceTypeHelper(const TreePtr<GenericType>& generic,
                          const PSI_STD::vector<TreePtr<Term> >& introduced_parameters,
                          const PSI_STD::vector<TreePtr<Term> >& generic_parameters)
      : m_generic(generic),
      m_introduced_parameters(introduced_parameters),
      m_generic_parameters(generic_parameters) {
      }
      
      TreePtr<Term> evaluate(const TreePtr<Term>& self) {
        TreePtr<Term> upref(new UpwardReference(self, int_to_index(0, self.compile_context(), self.location()), TreePtr<Term>(), self.location()));
        PSI_STD::vector<TreePtr<Term> > generic_parameters;
        generic_parameters.push_back(upref);
        generic_parameters.insert(generic_parameters.end(), m_generic_parameters.begin(), m_generic_parameters.end());
        TreePtr<Term> generic_instance(new TypeInstance(m_generic, generic_parameters, self.location()));
        TreePtr<Term> introduce_type;//(new IntroduceType(m_introduced_parameters, self.compile_context().builtins().empty_type, self.location()));
        PSI_STD::vector<TreePtr<Term> > pair_members;
        pair_members.push_back(generic_instance);
        pair_members.push_back(introduce_type);
        TreePtr<Term> pair_type(new StructType(self.compile_context(), pair_members, self.location()));
        return TreePtr<Term>(new DerivedType(pair_type, upref, self.location()));
      }
      
      template<typename V>
      static void visit(V& v) {
        v("generic", &InterfaceTypeHelper::m_generic)
        ("introduced_parameters", &InterfaceTypeHelper::m_introduced_parameters)
        ("generic_parameters", &InterfaceTypeHelper::m_generic_parameters);
      }
    };

    /**
     * \brief Construct the type of a specific implementation of an interface.
     * 
     * Global implementations of an interface will usually include additional type information via an
     * IntroduceType term. This facilitates the construction of that term by supplying the required
     * circular callback structure.
     * 
     * This assumes that the first parameter to the generic type is an upref.
     * 
     * \param generic Generic type of the interface.
     * \param introduced_parameters Type parameters which must be introduced in interface callback functions.
     * This should be a list of parameterised types as would be given in a function type.
     * \param generic_parameters Parameters to the generic type, excluding the first (upref) parameter which
     * this utility function is designed to handle.
     */
    TreePtr<Term> interface_type(const TreePtr<GenericType>& generic,
                                 const PSI_STD::vector<TreePtr<Term> >& introduced_parameters,
                                 const PSI_STD::vector<TreePtr<Term> >& generic_parameters) {
      return tree_callback(generic.compile_context(), generic.location(), InterfaceTypeHelper(generic, introduced_parameters, generic_parameters));
    }
  }
}
