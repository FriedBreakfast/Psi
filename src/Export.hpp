#ifndef H_PSI_EXPORT
#define H_PSI_EXPORT

#include "CppCompiler.hpp"

#ifdef psi_assert_EXPORTS
#define PSI_ASSERT_EXPORT_ATTR dllexport
#else
#define PSI_ASSERT_EXPORT_ATTR dllimport
#endif
#define PSI_ASSERT_EXPORT PSI_ATTRIBUTE((PSI_ASSERT_EXPORT_ATTR))

#ifdef psi_compiler_common_EXPORTS
#define PSI_COMPILER_COMMON_EXPORT_ATTR dllexport
#else
#define PSI_COMPILER_COMMON_EXPORT_ATTR dllimport
#endif
#define PSI_COMPILER_COMMON_EXPORT PSI_ATTRIBUTE((PSI_COMPILER_COMMON_EXPORT_ATTR))

#ifdef psi_compiler_EXPORTS
#define PSI_COMPILER_EXPORT_ATTR dllexport
#else
#define PSI_COMPILER_EXPORT_ATTR dllimport
#endif
#define PSI_COMPILER_EXPORT PSI_ATTRIBUTE((PSI_COMPILER_EXPORT_ATTR))

#ifdef psi_tvm_EXPORTS
#define PSI_TVM_EXPORT_ATTR dllexport
#else
#define PSI_TVM_EXPORT_ATTR dllimport
#endif
#define PSI_TVM_EXPORT PSI_ATTRIBUTE((PSI_TVM_EXPORT_ATTR))

#endif
