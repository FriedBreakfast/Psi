#ifndef HPP_PSI_CONFIGURATION
#define HPP_PSI_CONFIGURATION

#include "Export.hpp"
#include "PropertyValue.hpp"

namespace Psi {
PSI_COMPILER_COMMON_EXPORT void configuration_builtin(PropertyValue& pv);
PSI_COMPILER_COMMON_EXPORT void configuration_read_files(PropertyValue& pv);
}

#endif
