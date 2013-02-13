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
 * \brief Indices of members in the Movable interface
 */
enum InterfaceMovableMembers {
  interface_movable_init=0,
  interface_movable_fini=1,
  interface_movable_move_init=2,
  interface_movable_move=3
};

/**
 * \brief Indices of members in the Copyable interface
 */
enum InterfaceCopyableMembers {
  interface_copyable_copy_init=0,
  interface_copyable_copy=1
};

/**
 * \brief Helper class for implementing interfaces.
 */
class ImplementationHelper {
  SourceLocation m_location;
  TreePtr<GenericType> m_generic;
  TreePtr<TypeInstance> m_generic_instance;
  PSI_STD::vector<TreePtr<Anonymous> > m_pattern_parameters;
  PSI_STD::vector<TreePtr<Term> > m_generic_parameters;
  PSI_STD::vector<TreePtr<InterfaceValue> > m_pattern_interfaces;
  
  PSI_STD::vector<TreePtr<Term> > m_wrapper_member_types, m_wrapper_member_values;
  
  TreePtr<GenericType> m_wrapper_generic;
  TreePtr<StructType> m_wrapper_struct;
  TreePtr<TypeInstance> m_wrapper_instance;
  
public:
  ImplementationHelper(const SourceLocation& location,
                        const TreePtr<GenericType>& generic,
                        const PSI_STD::vector<TreePtr<Anonymous> >& pattern_parameters,
                        const PSI_STD::vector<TreePtr<Term> >& generic_parameters,
                        const PSI_STD::vector<TreePtr<InterfaceValue> >& pattern_interfaces);
  
  struct FunctionSetup {
    SourceLocation location;
    TreePtr<FunctionType> function_type;
    PSI_STD::vector<TreePtr<Anonymous> > parameters;
    PSI_STD::vector<TreePtr<Statement> > interface_values;
    TreePtr<EvaluateContext> context;
  };
  
  FunctionSetup function_setup(const TreePtr<FunctionType>& type, const TreePtr<EvaluateContext>& context,
                                const SourceLocation& location, const PSI_STD::vector<SourceLocation>& parameter_locations);
  TreePtr<Term> function_finish(const ImplementationHelper::FunctionSetup& setup, const TreePtr<Term>& body, const TreePtr<JumpTarget>& return_target=TreePtr<JumpTarget>());
  
  TreePtr<Implementation> finish(const TreePtr<Interface>& interface, const TreePtr<Term>& inner_value);
};
}
}

#endif
