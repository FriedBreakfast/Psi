#include "Implementation.hpp"
#include "TermBuilder.hpp"

namespace Psi {
namespace Compiler {
/**
  * \brief Begin generating a function for use in an interface.
  * 
  * The first argument to the function type \c type is assumed to be the interface
  * reference.
  */
ImplementationFunctionSetup implementation_function_setup(const TreePtr<FunctionType>& type, const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations) {
  ImplementationFunctionSetup result;
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

TreePtr<Term> implementation_function_finish(const ImplementationSetup& impl_setup, const ImplementationFunctionSetup& setup, const TreePtr<Module>& module, const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target) {
  TreePtr<Term> wrapped_body = body;
  int offset = 0;
  PSI_STD::vector<TreePtr<Term> > solidify_values;
  for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = impl_setup.pattern_parameters.begin(), ie = impl_setup.pattern_parameters.end(); ii != ie; ++ii, ++offset)
    solidify_values.push_back(TermBuilder::ptr_target(TermBuilder::element_pointer(setup.implementation, offset, setup.location), setup.location));
  if (!solidify_values.empty())
    wrapped_body = TermBuilder::solidify_during(solidify_values, wrapped_body, setup.location);
  
  PSI_STD::vector<TreePtr<Implementation> > implementations;
  for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = impl_setup.pattern_interfaces.begin(), ie = impl_setup.pattern_interfaces.end(); ii != ie; ++ii, ++offset) {
    TreePtr<Term> ptr_value = TermBuilder::ptr_target(TermBuilder::element_pointer(setup.implementation, offset, setup.location), setup.location);
    TreePtr<Term> value = TermBuilder::ptr_target(ptr_value, setup.location);
    implementations.push_back(Implementation::new_((*ii)->interface, OverloadPattern(0, (*ii)->parameters),
                                                   default_, ImplementationValue(value, true), setup.location));
  }
  
  TreePtr<Term> inner_implementation = setup.parameters.front();
  for (PSI_STD::vector<Interface::InterfaceBase>::const_iterator ii = impl_setup.interface->bases.begin(), ie = impl_setup.interface->bases.end(); ii != ie; ++ii) {
    TreePtr<Term> value = inner_implementation;
    for (PSI_STD::vector<int>::const_iterator ji = ii->path.begin(), je = ii->path.end(); ji != je; ++ji)
      value = TermBuilder::element_pointer(value, *ji, setup.location);
    value = TermBuilder::ptr_target(value, setup.location);
    PSI_STD::vector<TreePtr<Term> > parameters;
    for (PSI_STD::vector<TreePtr<Term> >::const_iterator ji = ii->parameters.begin(), je = ii->parameters.end(); ji != je; ++ji)
      parameters.push_back((*ji)->specialize(setup.location, impl_setup.interface_parameters));
    TreePtr<Implementation> impl = Implementation::new_(ii->interface, OverloadPattern(0, parameters),
                                                        default_, ImplementationValue(value, true), setup.location);
    implementations.push_back(impl);
  }
  
  if (!implementations.empty())
    wrapped_body = TermBuilder::introduce_implementation(implementations, wrapped_body, setup.location);
  
  PSI_STD::vector<TreePtr<Anonymous> > parameters = impl_setup.pattern_parameters;
  parameters.insert(parameters.end(), setup.parameters.begin(), setup.parameters.end());
  
  PSI_STD::vector<FunctionParameterType> parameter_types;
  for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = impl_setup.pattern_parameters.begin(), ie = impl_setup.pattern_parameters.end(); ii != ie; ++ii)
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

/**
 * Generate a parameterized interface pattern from the pattern plus a parameter list.
 */
OverloadPattern implementation_overload_pattern(const PSI_STD::vector<TreePtr<Term> >& pattern, const PSI_STD::vector<TreePtr<Anonymous> >& wildcards, const SourceLocation& location) {
  OverloadPattern result;
  
  result.n_wildcards = wildcards.size();
  for (PSI_STD::vector<TreePtr<Term> >::const_iterator ii = pattern.begin(), ie = pattern.end(); ii != ie; ++ii)
    result.pattern.push_back((*ii)->parameterize(location, wildcards));
  
  return result;
}

namespace {
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
}

/**
  * \param generic_parameters Parameters to the interface generic type.
  * This should have one element fewer than the number of parameters to the
  * generic itself: the first parameter is expected to be an upward reference
  * to the outer data structure which is filled in here.
  * 
  * \param pattern_interfaces Additional interfaces (further to those required
  * by the general interface) which the implementation depends upon.
  */
ImplementationHelper::ImplementationHelper(const ImplementationSetup& setup, const SourceLocation& location)
: m_setup(setup), m_location(location) {
  CompileContext& compile_context = m_setup.interface->compile_context();
  
  PSI_STD::vector<TreePtr<Term> > type_pattern;
  
  if (TreePtr<Exists> interface_exists = term_unwrap_dyn_cast<Exists>(m_setup.interface->type))
    if (TreePtr<PointerType> interface_ptr = term_unwrap_dyn_cast<PointerType>(interface_exists->result))
      if (TreePtr<TypeInstance> interface_inst = term_unwrap_dyn_cast<TypeInstance>(interface_ptr->target_type))
        m_generic = interface_inst->generic;
    
  if (!m_generic)
    compile_context.error_throw(location, "ImplementationHelper is only suitable for interfaces whose value is of the form Exists.PointerType.Instance", CompileError::error_internal);
  
  PSI_STD::vector<TreePtr<Term> > member_types;
  for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = m_setup.pattern_parameters.begin(), ie = m_setup.pattern_parameters.end(); ii != ie; ++ii) {
    TreePtr<Term> parameterized = (*ii)->parameterize(location, m_setup.pattern_parameters);
    type_pattern.push_back(parameterized);
    TreePtr<Term> type = TermBuilder::constant(parameterized, location);
    member_types.push_back(type);
    m_wrapper_member_values.push_back(TermBuilder::default_value(type, location));
  }
  
  for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = m_setup.pattern_interfaces.begin(), ie = m_setup.pattern_interfaces.end(); ii != ie; ++ii) {
    TreePtr<Term> value = (*ii)->parameterize(location, m_setup.pattern_parameters);
    member_types.push_back(value->type);
    m_wrapper_member_values.push_back(value);
  }
  
  m_overload_pattern = implementation_overload_pattern(m_setup.interface_parameters, m_setup.pattern_parameters, location);

  m_wrapper_generic = TermBuilder::generic(compile_context, type_pattern, GenericType::primitive_always, location,
                                           ImplementationHelperWrapperGeneric(type_pattern, member_types, m_generic, m_overload_pattern.pattern));
  
  TreePtr<Term> wrapper_instance = TermBuilder::instance(m_wrapper_generic, type_pattern, location);
  
  // Need a double upward reference: one for the struct and one for the containing generic
  TreePtr<Term> upref = TermBuilder::upref(wrapper_instance, 0, TermBuilder::upref_null(compile_context), location);
  upref = TermBuilder::upref(default_, m_wrapper_member_values.size(), upref, location);
  
  m_generic_parameters = m_setup.interface_parameters;
  m_generic_parameters.insert(m_generic_parameters.begin(), upref);
}

ImplementationValue ImplementationHelper::finish_value(const TreePtr<Term>& inner_value) {
  TreePtr<TypeInstance> inner_generic_instance =
    treeptr_cast<TypeInstance>(treeptr_cast<StructType>(m_wrapper_generic->member_type())->members[m_wrapper_member_values.size()]);
  TreePtr<Term> inner_value_parameterized = inner_value->parameterize(m_location, m_setup.pattern_parameters);
  m_wrapper_member_values.push_back(TermBuilder::instance_value(inner_generic_instance, inner_value_parameterized, m_location));
  
  PSI_STD::vector<TreePtr<Term> > type_pattern;
  for (PSI_STD::vector<TreePtr<Anonymous> >::const_iterator ii = m_setup.pattern_parameters.begin(), ie = m_setup.pattern_parameters.end(); ii != ie; ++ii)
    type_pattern.push_back((*ii)->parameterize(m_location, m_setup.pattern_parameters));

  TreePtr<Term> struct_value = TermBuilder::struct_value(inner_value->compile_context(), m_wrapper_member_values, m_location);
  TreePtr<TypeInstance> wrapper_instance = TermBuilder::instance(m_wrapper_generic, type_pattern, m_location);
  TreePtr<Term> value = TermBuilder::instance_value(wrapper_instance, struct_value, m_location);
  
  return ImplementationValue(value, vector_of<unsigned>(0,m_wrapper_member_values.size()-1));
}

TreePtr<Implementation> ImplementationHelper::finish(const TreePtr<Term>& inner_value) {
  return Implementation::new_(m_setup.interface, m_overload_pattern,
                              default_, finish_value(inner_value), m_location);
}

TreePtr<Term> ImplementationHelper::member_type(int index, const SourceLocation& location) {
  TreePtr<StructType> st = term_unwrap_dyn_cast<StructType>(m_generic->member_type());
  if (!st)
    m_generic->compile_context().error_throw(location, "ImplementationHelper::member_function_type used on generic which is not a struct", CompileError::error_internal);
  
  return st->members[index]->specialize(location, m_generic_parameters);
}

TreePtr<FunctionType> ImplementationHelper::member_function_type(int index, const SourceLocation& location) {
  TreePtr<Term> mt = member_type(index, location);

  TreePtr<PointerType> pt = term_unwrap_dyn_cast<PointerType>(mt);
  if (!pt)
    m_generic->compile_context().error_throw(location, "ImplementationHelper::member_function_type member index does not lead to a pointer", CompileError::error_internal);
  
  TreePtr<FunctionType> ft = term_unwrap_dyn_cast<FunctionType>(pt->target_type);
  if (!ft)
    m_generic->compile_context().error_throw(location, "ImplementationHelper::member_function_type member index does not lead to a function pointer", CompileError::error_internal);
  
  return ft;
}

/**
  * Shortcut for:
  * 
  * \code this->function_setup(this->member_function_type(index, location), location, parameter_locations) \endcode
  */
ImplementationFunctionSetup ImplementationHelper::member_function_setup(int index, const SourceLocation& location,
                                                                        const PSI_STD::vector<SourceLocation>& parameter_locations) {
  return implementation_function_setup(member_function_type(index, location), location, parameter_locations);
}

TreePtr<Term> ImplementationHelper::member_function_finish(const ImplementationFunctionSetup& setup, const TreePtr<Module>& module, const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target) {
  return implementation_function_finish(m_setup, setup, module, body, return_target);
}
}
}
