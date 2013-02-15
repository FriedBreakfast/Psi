#ifndef HPP_PSI_INTERFACE
#define HPP_PSI_INTERFACE

#include "Compiler.hpp"
#include "Tree.hpp"

/**
 * \file
 * 
 * Place for interfaces which have no better place to go.
 */

namespace Psi {
namespace Compiler {
/**
 * \brief Helper class for implementing interfaces.
 */
class ImplementationHelper {
  SourceLocation m_location;
  TreePtr<Interface> m_interface;
  TreePtr<GenericType> m_generic;
  TreePtr<TypeInstance> m_generic_instance;
  TreePtr<Term> m_generic_unwrapped;
  PSI_STD::vector<TreePtr<Anonymous> > m_pattern_parameters;
  PSI_STD::vector<TreePtr<Term> > m_generic_parameters;
  PSI_STD::vector<TreePtr<InterfaceValue> > m_pattern_interfaces;
  
  PSI_STD::vector<TreePtr<Term> > m_wrapper_member_types, m_wrapper_member_values;
  
  TreePtr<GenericType> m_wrapper_generic;
  TreePtr<StructType> m_wrapper_struct;
  TreePtr<TypeInstance> m_wrapper_instance;
  
public:
  ImplementationHelper(const SourceLocation& location,
                       const TreePtr<Interface>& interface,
                       const PSI_STD::vector<TreePtr<Anonymous> >& pattern_parameters,
                       const PSI_STD::vector<TreePtr<Term> >& generic_parameters,
                       const PSI_STD::vector<TreePtr<InterfaceValue> >& pattern_interfaces);
  
  struct FunctionSetup {
    SourceLocation location;
    TreePtr<FunctionType> function_type;
    TreePtr<Term> implementation;
    PSI_STD::vector<TreePtr<Anonymous> > parameters;
    PSI_STD::vector<TreePtr<Statement> > interface_values;
  };
  
  FunctionSetup member_function_setup(int index, const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations);
  TreePtr<FunctionType> member_function_type(int index, const SourceLocation& location);
  FunctionSetup function_setup(const TreePtr<FunctionType>& type, const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations);
  TreePtr<Term> function_finish(const ImplementationHelper::FunctionSetup& setup, const TreePtr<Module>& module, const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target=TreePtr<JumpTarget>());
  
  TreePtr<Implementation> finish(const TreePtr<Term>& inner_value);
};
}
}

#endif
