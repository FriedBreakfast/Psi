#include "Compiler.hpp"
#include "Interface.hpp"
#include "Tree.hpp"
#include "TermBuilder.hpp"
#include "Macros.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * Parse generic arguments to an aggregate type.
     */
    PatternArguments parse_pattern_arguments(const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location, const Parser::Text& text) {
      PatternArguments result;

      PSI_STD::vector<SharedPtr<Parser::FunctionArgument> > generic_parameters_parsed =
        Parser::parse_type_argument_declarations(evaluate_context->compile_context().error_context(), location.logical, text);
        
      for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = generic_parameters_parsed.begin(), ie = generic_parameters_parsed.end(); ii != ie; ++ii) {
        PSI_ASSERT(*ii && (*ii)->type);
        const Parser::FunctionArgument& argument_expr = **ii;
        
        String expr_name;
        LogicalSourceLocationPtr argument_logical_location;
        if (argument_expr.name) {
          expr_name = String(argument_expr.name->begin, argument_expr.name->end);
          argument_logical_location = location.logical->new_child(expr_name);
        } else {
          argument_logical_location = location.logical;
        }
        SourceLocation argument_location(argument_expr.location, argument_logical_location);

        if (argument_expr.mode != parameter_mode_input)
          evaluate_context->compile_context().error_throw(argument_location, "Generic type parameters must be declared with ':'");

        TreePtr<EvaluateContext> argument_context = evaluate_context_dictionary(evaluate_context->module(), argument_location, result.names, evaluate_context);
        TreePtr<Term> argument_type = compile_term(argument_expr.type, argument_context, argument_location.logical);
        result.pattern.push_back(argument_type->parameterize(argument_location, result.list));
        TreePtr<Anonymous> argument = TermBuilder::anonymous(argument_type, term_mode_value, argument_location);
        result.list.push_back(argument);

        if (argument_expr.name)
          result.names[expr_name] = argument;
      }
      
      return result;
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
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        if (parameters.size() != 2)
          self.compile_context().error_throw(location, "Interface definition expects 2 parameters");

        SharedPtr<Parser::TokenExpression> types_expr, members_expr;
        if (!(types_expr = expression_as_token_type(parameters[0], Parser::token_bracket)))
          self.compile_context().error_throw(location, "First (types) parameter to interface macro is not a (...)");
        if (!(members_expr = expression_as_token_type(parameters[1], Parser::token_square_bracket)))
          self.compile_context().error_throw(location, "Second (members) parameter to interface macro is not a [...]");

        PatternArguments args = parse_pattern_arguments(evaluate_context, location, types_expr->text);
        TreePtr<EvaluateContext> member_context = evaluate_context_dictionary(evaluate_context->module(), location, args.names, evaluate_context);	  

        PSI_STD::vector<SharedPtr<Parser::Statement> > members = Parser::parse_statement_list(self.compile_context().error_context(), location.logical, members_expr->text);
        
        PSI_NOT_IMPLEMENTED();
      }
    };

    const MacroMemberCallbackVtable InterfaceDefineMacro::vtable = PSI_COMPILER_MACRO_MEMBER_CALLBACK(InterfaceDefineMacro, "psi.compiler.InterfaceDefineMacro", MacroMemberCallback);

    /**
     * Return a term which is a macro for defining new interfaces.
     */
    TreePtr<Term> interface_define_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(::new InterfaceDefineMacro(compile_context, location));
      TreePtr<Macro> m = make_macro(compile_context, location, callback);
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
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
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
      TreePtr<MacroMemberCallback> callback(::new ImplementationDefineMacro(compile_context, location));
      TreePtr<Macro> m = make_macro(compile_context, location, callback);
      return make_macro_term(m, location);
    }
    
    class ImplementationHelperWrapperGeneric {
      PSI_STD::vector<TreePtr<Term> > m_pattern_parameters;
      PSI_STD::vector<TreePtr<Term> > m_members;

      TreePtr<GenericType> m_inner_generic;
      PSI_STD::vector<TreePtr<Term> > m_inner_parameters;

    public:
      typedef GenericType TreeResultType;
      
      ImplementationHelperWrapperGeneric(const PSI_STD::vector<TreePtr<Term> >& pattern_parameters, const PSI_STD::vector<TreePtr<Term> >& members,
                                         const TreePtr<GenericType>& inner_generic, const PSI_STD::vector<TreePtr<Term> >& inner_parameters)
      : m_pattern_parameters(pattern_parameters), m_members(members),
      m_inner_generic(inner_generic), m_inner_parameters(inner_parameters) {
      }
      
      TreePtr<Term> evaluate(const TreePtr<GenericType>& self) {
        TreePtr<Term> instance = TermBuilder::instance(self, m_pattern_parameters, self->location());
        TreePtr<Term> upref = TermBuilder::upref(instance, 0, TermBuilder::upref_null(self->compile_context()), self->location());
        upref = TermBuilder::upref(default_, m_members.size(), upref, self->location());
        m_inner_parameters.insert(m_inner_parameters.begin(), upref);
        TreePtr<Term> inner_instance = TermBuilder::instance(m_inner_generic, m_inner_parameters, self->location());
        m_members.push_back(inner_instance);
        TreePtr<Term> wrapper_struct = TermBuilder::struct_type(self->compile_context(), m_members, self->location());
        return wrapper_struct;
      }
      
      template<typename V>
      static void visit(V& v) {
        v("pattern_parameters", &ImplementationHelperWrapperGeneric::m_pattern_parameters)
        ("members", &ImplementationHelperWrapperGeneric::m_members)
        ("inner_generic", &ImplementationHelperWrapperGeneric::m_inner_generic)
        ("inner_parameters", &ImplementationHelperWrapperGeneric::m_inner_parameters);
      }
    };
    
    /**
     * \param generic_parameters Parameters to the interface generic type.
     * This should have one element fewer than the number of parameters to the
     * generic itself: the first parameter is expected to be an upward reference
     * to the outer data structure which is filled in here.
     */
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
      
      if (TreePtr<Exists> interface_exists = term_unwrap_dyn_cast<Exists>(m_interface->type))
        if (TreePtr<PointerType> interface_ptr = term_unwrap_dyn_cast<PointerType>(interface_exists->result))
          if (TreePtr<TypeInstance> interface_inst = term_unwrap_dyn_cast<TypeInstance>(interface_ptr->target_type))
            m_generic = interface_inst->generic;
        
      if (!m_generic)
        interface->compile_context().error_throw(location, "ImplementationHelper is only suitable for interfaces whose value is of the form Exists.PointerType.Instance", CompileError::error_internal);
      
      PSI_STD::vector<TreePtr<Term> > member_types;
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = pattern_parameters.begin(), ie = pattern_parameters.end(); ii != ie; ++ii) {
        TreePtr<Term> parameterized = (*ii)->parameterize(location, pattern_parameters);
        type_pattern.push_back(parameterized);
        TreePtr<Term> type = TermBuilder::constant(parameterized, location);
        member_types.push_back(type);
        m_wrapper_member_values.push_back(TermBuilder::default_value(type, location));
      }
      
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = pattern_interfaces.begin(), ie = pattern_interfaces.end(); ii != ie; ++ii) {
        TreePtr<Term> value = (*ii)->parameterize(location, pattern_parameters);
        member_types.push_back(value->type);
        m_wrapper_member_values.push_back(value);
      }
      
      for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = generic_parameters.begin(), ie = generic_parameters.end(); ii != ie; ++ii)
        m_interface_parameters.push_back((*ii)->parameterize(location, pattern_parameters));

      m_wrapper_generic = TermBuilder::generic(interface->compile_context(), type_pattern, GenericType::primitive_always, location,
                                               ImplementationHelperWrapperGeneric(type_pattern, member_types, m_generic, m_interface_parameters));
      
      TreePtr<Term> wrapper_instance = TermBuilder::instance(m_wrapper_generic, type_pattern, location);
      
      // Need a double upward reference: one for the struct and one for the containing generic
      TreePtr<Term> upref = TermBuilder::upref(wrapper_instance, 0, TermBuilder::upref_null(interface->compile_context()), location);
      upref = TermBuilder::upref(default_, m_wrapper_member_values.size(), upref, location);
      
      m_generic_parameters.insert(m_generic_parameters.begin(), upref);
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
        const SourceLocation& loc = (ie - ii) <= parameter_locations.size() ?
          parameter_locations[parameter_locations.size() - (ie - ii)] : location;
        TreePtr<Anonymous> param = type->parameter_after(loc, previous_arguments);
        previous_arguments.push_back(param);
        result.parameters.push_back(param);
      }
      
      result.implementation = TermBuilder::outer_pointer(result.parameters.front(), result.parameters.front()->location());
      
      return result;
    }
    
    TreePtr<Term> ImplementationHelper::function_finish(const ImplementationHelper::FunctionSetup& setup, const TreePtr<Module>& module,
                                                        const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target) {
      TreePtr<Term> wrapped_body = body;
      int offset = 0;
      PSI_STD::vector<TreePtr<Term> > solidify_values;
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = m_pattern_parameters.begin(), ie = m_pattern_parameters.end(); ii != ie; ++ii, ++offset)
        solidify_values.push_back(TermBuilder::ptr_target(TermBuilder::element_pointer(setup.implementation, offset, setup.location), setup.location));
      if (!solidify_values.empty())
        wrapped_body = TermBuilder::solidify_during(solidify_values, wrapped_body, setup.location);
      
      PSI_STD::vector<TreePtr<Implementation> > implementations;
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = m_pattern_interfaces.begin(), ie = m_pattern_interfaces.end(); ii != ie; ++ii, ++offset) {
        TreePtr<Term> ptr_value = TermBuilder::ptr_target(TermBuilder::element_pointer(setup.implementation, offset, setup.location), setup.location);
        TreePtr<Term> value = TermBuilder::ptr_target(ptr_value, setup.location);
        implementations.push_back(Implementation::new_(default_, value, (*ii)->interface, 0, (*ii)->parameters, true, default_, setup.location));
      }
      
      TreePtr<Term> inner_implementation = TermBuilder::element_pointer(setup.implementation, m_wrapper_member_values.size(), setup.location);
      for (PSI_STD::vector<Interface::InterfaceBase>::const_iterator ii = m_interface->bases.begin(), ie = m_interface->bases.end(); ii != ie; ++ii) {
        TreePtr<Term> value = inner_implementation;
        for (PSI_STD::vector<int>::const_iterator ji = ii->path.begin(), je = ii->path.end(); ji != je; ++ji)
          value = TermBuilder::element_pointer(value, *ji, setup.location);
        value = TermBuilder::ptr_target(value, setup.location);
        PSI_STD::vector<TreePtr<Term> > parameters;
        for (PSI_STD::vector<TreePtr<Term> >::const_iterator ji = ii->parameters.begin(), je = ii->parameters.end(); ji != je; ++ji)
          parameters.push_back((*ji)->specialize(setup.location, m_generic_parameters));
        TreePtr<Implementation> impl = Implementation::new_(default_, value, ii->interface, 0, parameters, true, default_, setup.location);
        implementations.push_back(impl);
      }
      
      if (!implementations.empty())
        wrapped_body = TermBuilder::introduce_implementation(implementations, wrapped_body, setup.location);
      
      PSI_STD::vector<TreePtr<Anonymous> > parameters = m_pattern_parameters;
      parameters.insert(parameters.end(), setup.parameters.begin(), setup.parameters.end());
      
      PSI_STD::vector<FunctionParameterType> parameter_types;
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = m_pattern_parameters.begin(), ie = m_pattern_parameters.end(); ii != ie; ++ii)
        parameter_types.push_back(FunctionParameterType(parameter_mode_phantom, (*ii)->type->parameterize(setup.location, parameters)));

      for (std::size_t ii = 0, ie = setup.parameters.size(); ii != ie; ++ii)
        parameter_types.push_back(FunctionParameterType(setup.function_type->parameter_types[ii].mode, setup.parameters[ii]->type->parameterize(setup.location, parameters)));
      
      PSI_STD::vector<TreePtr<InterfaceValue> > function_interfaces;
      PSI_STD::vector<TreePtr<Term> > setup_parameters_term(setup.parameters.begin(), setup.parameters.end());
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = setup.function_type->interfaces.begin(), ie = setup.function_type->interfaces.end(); ii != ie; ++ii)
        function_interfaces.push_back(treeptr_cast<InterfaceValue>((*ii)->specialize(setup.location, setup_parameters_term)->parameterize(setup.location, parameters)));
      
      TreePtr<Term> result_type = setup.function_type->result_type_after(setup.location, setup_parameters_term)->parameterize(setup.location, parameters);
      TreePtr<FunctionType> function_type = TermBuilder::function_type(setup.function_type->result_mode, result_type, parameter_types, function_interfaces, setup.location);
      /// \todo Implementation functions should inherit their linkage from the implementation
      TreePtr<Global> function = TermBuilder::function(module, function_type, link_private, parameters, return_target, setup.location, wrapped_body);
      return TermBuilder::ptr_to(function, setup.location);
    }
    
    TreePtr<Implementation> ImplementationHelper::finish(const TreePtr<Term>& inner_value) {
      TreePtr<TypeInstance> inner_generic_instance =
        treeptr_cast<TypeInstance>(treeptr_cast<StructType>(m_wrapper_generic->member_type())->members[m_wrapper_member_values.size()]);
      TreePtr<Term> inner_value_parameterized = inner_value->parameterize(m_location, m_pattern_parameters);
      m_wrapper_member_values.push_back(TermBuilder::instance_value(inner_generic_instance, inner_value_parameterized, m_location));
      
      PSI_STD::vector<TreePtr<Term> > type_pattern;
      for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = m_pattern_parameters.begin(), ie = m_pattern_parameters.end(); ii != ie; ++ii)
        type_pattern.push_back((*ii)->parameterize(m_location, m_pattern_parameters));

      TreePtr<Term> struct_value = TermBuilder::struct_value(inner_value->compile_context(), m_wrapper_member_values, m_location);
      TreePtr<TypeInstance> wrapper_instance = TermBuilder::instance(m_wrapper_generic, type_pattern, m_location);
      TreePtr<Term> value = TermBuilder::instance_value(wrapper_instance, struct_value, m_location);
      
      return Implementation::new_(default_, value, m_interface, 0, m_interface_parameters, false,
                                  vector_of<int>(0,m_wrapper_member_values.size()-1), m_location);
    }

    TreePtr<FunctionType> ImplementationHelper::member_function_type(int index, const SourceLocation& location) {
      TreePtr<StructType> st = term_unwrap_dyn_cast<StructType>(m_generic->member_type());
      if (!st)
        m_generic->compile_context().error_throw(location, "ImplementationHelper::member_function_type used on generic which is not a struct", CompileError::error_internal);
      
      TreePtr<PointerType> pt = term_unwrap_dyn_cast<PointerType>(st->members[index]);
      if (!pt)
        m_generic->compile_context().error_throw(location, "ImplementationHelper::member_function_type member index does not lead to a pointer", CompileError::error_internal);
      
      TreePtr<FunctionType> ft = term_unwrap_dyn_cast<FunctionType>(pt->target_type);
      if (!ft)
        m_generic->compile_context().error_throw(location, "ImplementationHelper::member_function_type member index does not lead to a function pointer", CompileError::error_internal);
      
      return treeptr_cast<FunctionType>(pt->target_type->specialize(location, m_generic_parameters));
    }
    
    /**
     * Shortcut for:
     * 
     * \code this->function_setup(this->member_function_type(index, location), location, parameter_locations) \endcode
     */
    ImplementationHelper::FunctionSetup ImplementationHelper::member_function_setup(int index, const SourceLocation& location,
                                                                                    const PSI_STD::vector<SourceLocation>& parameter_locations) {
      return function_setup(member_function_type(index, location), location, parameter_locations);
    }
  }
}
