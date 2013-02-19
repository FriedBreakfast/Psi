#ifndef HPP_PSI_COMPILER_ENUMS
#define HPP_PSI_COMPILER_ENUMS

#include "Visitor.hpp"

/**
 * \file
 */

namespace Psi {
  namespace Compiler {
    /**
     * \brief Storage modes for function parameters.
     * 
     * \see \ref storage_specifiers
     */
    enum ParameterMode {
      parameter_mode_input, ///< Input parameter
      parameter_mode_output, ///< Output parameter
      parameter_mode_io, ///< Input/Output parameter
      parameter_mode_rvalue, ///< R-value reference
      parameter_mode_functional, ///< Funtional value
      parameter_mode_phantom ///< Phantom value
    };
    
    PSI_VISIT_SIMPLE(ParameterMode);
    
    /**
     * \brief Storage modes for function return values and jump parameters.
     *
     * \see \ref storage_specifiers
     */
    enum ResultMode {
      result_mode_by_value, ///< By value
      result_mode_functional, ///< By value, functional
      result_mode_rvalue, ///< R-value reference
      result_mode_lvalue ///< L-value reference
    };
    
    PSI_VISIT_SIMPLE(ResultMode);

    /**
     * \brief Storage modes for statements.
     */
    enum StatementMode {
      statement_mode_value, ///< Store (possibly a copy of) the result value
      statement_mode_functional, ///< Freeze result value
      statement_mode_ref, ///< Store the reference which is the result of this expression.
      statement_mode_destroy ///< Destroy result immediately after computation
    };
    
    PSI_VISIT_SIMPLE(StatementMode);
    
    /**
     * \brief Indices of members in the Movable interface
     */
    enum InterfaceMovableMembers {
      interface_movable_init=0,
      interface_movable_fini=1,
      interface_movable_clear=2,
      interface_movable_move_init=3,
      interface_movable_move=4
    };

    /**
     * \brief Indices of members in the Copyable interface
     */
    enum InterfaceCopyableMembers {
      interface_copyable_movable=0, // Reference to Movable interface for the same type
      interface_copyable_copy_init=1,
      interface_copyable_copy=2
    };
  }
  
  namespace Parser {
    enum ExpressionType {
      expression_token,
      expression_evaluate,
      expression_dot
    };

    enum TokenExpressionType {
      token_identifier,
      token_number,
      token_brace,
      token_square_bracket,
      token_bracket
    };
  }    
}

#endif
