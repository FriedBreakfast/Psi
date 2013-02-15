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
    
    ImplementationHelper::ImplementationHelper(const SourceLocation& location,
                                               const TreePtr<Interface>& interface,
                                               const PSI_STD::vector<TreePtr<Anonymous> >& pattern_parameters,
                                               const PSI_STD::vector<TreePtr<Term> >& generic_parameters,
                                               const PSI_STD::vector<TreePtr<InterfaceValue> >& pattern_interfaces)
    : m_location(location),
    m_interface(interface),
    m_pattern_parameters(pattern_parameters),
    m_generic_parameters(generic_parameters),
    m_pattern_interfaces(pattern_interfaces) {
      PSI_STD::vector<TreePtr<Term> > type_pattern;
      
      TreePtr<TypeInstance> interface_inst = dyn_treeptr_cast<TypeInstance>(m_interface->type);
      if (!interface_inst)
        interface.compile_context().error_throw(location, "ImplementationHelper is only suitable for interfaces whose value is a generic", CompileError::error_internal);
      m_generic = interface_inst->generic;
      
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = pattern_parameters.begin(), ie = pattern_parameters.end(); ii != ie; ++ii) {
        TreePtr<Term> parameterized = (*ii)->parameterize(location, pattern_parameters);
        type_pattern.push_back(parameterized);
        TreePtr<Term> type(new ConstantType(parameterized, location));
        m_wrapper_member_types.push_back(type);
        m_wrapper_member_values.push_back(TreePtr<Term>(new DefaultValue(type, location)));
      }
      
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = pattern_interfaces.begin(), ie = pattern_interfaces.end(); ii != ie; ++ii) {
        TreePtr<Term> value = (*ii)->parameterize(location, pattern_parameters);
        m_wrapper_member_types.push_back(value->type);
        m_wrapper_member_values.push_back(value);
      }
      
      m_generic_instance.reset(new TypeInstance(m_generic, generic_parameters, location));
      m_wrapper_member_types.push_back(m_generic_instance);
      
      m_wrapper_struct.reset(new StructType(m_generic.compile_context(), m_wrapper_member_types, location));
      m_wrapper_generic.reset(new GenericType(type_pattern, m_wrapper_struct, default_, GenericType::primitive_always, location));
      m_wrapper_instance.reset(new TypeInstance(m_wrapper_generic, vector_from<TreePtr<Term> >(pattern_parameters), location));
    }
    
    /**
     * \brief Begin generating a function for use in an interface.
     * 
     * The first argument to the function type \c type is assumed to be the interface
     * reference.
     */
    ImplementationHelper::FunctionSetup ImplementationHelper::function_setup(const TreePtr<FunctionType>& type, const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations) {
      FunctionSetup result;
      result.location = location;
      result.function_type = type;
      
      PSI_STD::vector<TreePtr<Term> > previous_arguments;
      for (std::size_t ii = 0, ie = type->parameter_types.size(); ii != ie; ++ii) {
        const SourceLocation& loc = (ie - ii - 1) < parameter_locations.size() ?
          parameter_locations[parameter_locations.size() - (ie - ii - 1)] : location;
        TreePtr<Term> ty = type->parameter_type_after(loc, previous_arguments);
        TreePtr<Anonymous> param(new Anonymous(ty, location));
        previous_arguments.push_back(param);
        result.parameters.push_back(param);
      }
      
      result.implementation.reset(new OuterValue(result.parameters.front(), result.parameters.front().location()));
      
      int offset = m_pattern_parameters.size();
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = m_pattern_interfaces.begin(), ie = m_pattern_interfaces.end(); ii != ie; ++ii, ++offset) {
        TreePtr<ElementValue> value(new ElementValue(result.implementation, offset, location));
        result.interface_values.push_back(TreePtr<Statement>(new Statement(value, statement_mode_functional, location)));
      }
      
      PSI_NOT_IMPLEMENTED(); // setup interfaces
      
      return result;
    }
    
    TreePtr<Term> ImplementationHelper::function_finish(const ImplementationHelper::FunctionSetup& setup, const TreePtr<Module>& module,
                                                        const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target) {
      TreePtr<Term> wrapped_body(new Block(setup.interface_values, body, setup.location));
      int offset = 0;
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = m_pattern_parameters.begin(), ie = m_pattern_parameters.end(); ii != ie; ++ii, ++offset) {
        TreePtr<ElementValue> value(new ElementValue(setup.implementation, offset, setup.location));
        wrapped_body.reset(new SolidifyDuring(value, wrapped_body, setup.location));
      }
      
      TreePtr<Function> f(new Function(module, false, setup.function_type,
                                       setup.parameters, wrapped_body, return_target, setup.location));
      return f;
    }
    
    TreePtr<Implementation> ImplementationHelper::finish(const TreePtr<Term>& inner_value) {
      TreePtr<Term> inner_value_parameterized = inner_value->parameterize(m_location, m_pattern_parameters, 1);
      TreePtr<Term> inner_instance(new TypeInstanceValue(m_generic_instance, inner_value_parameterized, m_location));
      m_wrapper_member_types.push_back(inner_instance);
      TreePtr<Term> value(new StructValue(m_wrapper_struct, m_wrapper_member_types, m_location));
      value.reset(new TypeInstanceValue(m_wrapper_instance, value, m_location));
      
      TreePtr<Implementation> impl(new Implementation(default_, value, m_interface, 0, default_, m_location));
      return impl;
    }
  }
}
