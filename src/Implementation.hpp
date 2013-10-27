#ifndef HPP_PSI_IMPLEMENTATION
#define HPP_PSI_IMPLEMENTATION

#include "Tree.hpp"
#include "Interface.hpp"

namespace Psi {
namespace Compiler {
struct ImplementationFunctionSetup {
  SourceLocation location;
  TreePtr<FunctionType> function_type;
  TreePtr<Term> implementation;
  PSI_STD::vector<TreePtr<Anonymous> > parameters;
};

ImplementationFunctionSetup implementation_function_setup(const TreePtr<FunctionType>& type, const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations=PSI_STD::vector<SourceLocation>());
TreePtr<Term> implementation_function_finish(const ImplementationSetup& impl_setup, const ImplementationFunctionSetup& setup, const TreePtr<Module>& module, const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target=TreePtr<JumpTarget>());

/**
 * \brief Helper class for implementing interfaces.
 */
class ImplementationHelper {
  ImplementationSetup m_setup;
  SourceLocation m_location;
  PSI_STD::vector<TreePtr<Term> > m_interface_parameters_pattern;

  TreePtr<GenericType> m_generic;
  PSI_STD::vector<TreePtr<Term> > m_generic_parameters;
  
  TreePtr<GenericType> m_wrapper_generic;
  PSI_STD::vector<TreePtr<Term> > m_wrapper_member_values;
  
public:
  ImplementationHelper(const ImplementationSetup& setup, const SourceLocation& location);
  
  /// \brief Get the location used to construct this helper.
  const SourceLocation& location() const {return m_location;}
  
  ImplementationFunctionSetup member_function_setup(int index, const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations);
  TreePtr<Term> member_function_finish(const ImplementationFunctionSetup& setup, const TreePtr<Module>& module, const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target=TreePtr<JumpTarget>());
  TreePtr<Term> member_type(int index, const SourceLocation& location);
  TreePtr<FunctionType> member_function_type(int index, const SourceLocation& location);
  
  TreePtr<Implementation> finish(const TreePtr<Term>& inner_value);
};
}
}

#endif
