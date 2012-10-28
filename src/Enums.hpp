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
      parameter_mode_functional ///< Funtional value
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
  }
}

#endif
