#ifndef HPP_PSI_TVM_OPERATIONS
#define HPP_PSI_TVM_OPERATIONS

#include "Functional.hpp"

namespace Psi {
  namespace Tvm {
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerAdd)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerSubtract)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerMultiply)
    PSI_TVM_FUNCTIONAL_TYPE_BINARY(IntegerDivide)

    PSI_TVM_FUNCTIONAL_TYPE(FunctionSpecialize)
    typedef Empty Data;
    PSI_TVM_FUNCTIONAL_PTR_HOOK()
    /// \brief Get the function being specialized.
    Term* function() const {return get()->parameter(0);}
    /// \brief Get the number of parameters being applied.
    std::size_t n_parameters() const {return get()->n_parameters() - 1;}
    /// \brief Get the value of the <tt>n</tt>th parameter being applied.
    Term* parameter(std::size_t n) const {return get()->parameter(n+1);}
    PSI_TVM_FUNCTIONAL_PTR_HOOK_END()
    PSI_TVM_FUNCTIONAL_TYPE_END(FunctionSpecialize)
  }
}

#endif
