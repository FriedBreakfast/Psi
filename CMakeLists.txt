cmake_minimum_required(VERSION 2.6)
project(Psi)

include(CPack)
include(CTest)

include(DetectPlatform.cmake)

# Enable -fvisibility=hidden on GCC
include(GenerateExportHeader)
add_compiler_export_flags()

set(PSI_DEBUG 0 CACHE BOOL "Enable debug checks")
set(PSI_ENABLE_VALGRIND 0 CACHE BOOL "Enable Valgrind support")
set(PSI_OBJECT_PTR_DEBUG 0 CACHE BOOL "Enable object pointer debug checks (very expensive)")
mark_as_advanced(PSI_OBJECT_PTR_DEBUG)

# Locate LLVM
set(PSI_LLVM_MODULES engine ipo)
if(WIN32)
  find_path(LLVM_PATH share/llvm/cmake/LLVMConfig.cmake)
  if(LLVM_PATH)
    set(PSI_TVM_LLVM 1)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_PATH}/share/llvm/cmake")
    include(LLVMConfig)
    set(LLVM_CXX_FLAGS -I${LLVM_INCLUDE_DIRS} -D${LLVM_DEFINITIONS})
    set(LLVM_LDFLAGS -LIBPATH:${LLVM_LIBRARY_DIRS})
    llvm_map_components_to_libraries(LLVM_LIBS ${PSI_LLVM_MODULES})
  else()
    set(PSI_TVM_LLVM 0)
  endif()
else()
  find_program(LLVM_CONFIG NAMES llvm-config)
  if(LLVM_CONFIG)
    set(PSI_TVM_LLVM 1)
    execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --libs ${PSI_LLVM_MODULES} OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
  else()
    set(PSI_TVM_LLVM 0)
  endif()
endif()

# Find TCC
if(CMAKE_CXX_COMPILER_IS_GCC)
  set(PSI_TVM_CC_SYSTEM_KIND_DEFAULT "gcc")
endif()

set(PSI_TVM_CC_SYSTEM_PATH ${CMAKE_C_COMPILER} CACHE FILEPATH "Path to system C compiler")
set(PSI_TVM_CC_SYSTEM_KIND ${PSI_TVM_CC_SYSTEM_KIND_DEFAULT} CACHE STRING "Kind of C compiler at PSI_TVM_CC_SYSTEM_PATH, should be 'gcc', 'clang' or 'tcc'")

find_library(TCC_LIB tcc)
find_path(TCC_INCLUDE libtcc.h)
find_path(TCC_PATH libtcc1.a)
find_path(TCC_CC_INCLUDE stdio.h)
if(TCC_LIB AND TCC_INCLUDE AND TCC_PATH AND TCC_CC_INCLUDE)
  # Whether TCC has been compiled in to the C backend
  set(PSI_TVM_CC_TCCLIB 1)
  set(PSI_TVM_CC_TCC_INCLUDE ${TCC_CC_INCLUDE})
  set(PSI_TVM_CC_TCC_PATH ${TCC_PATH})
else()
  set(PSI_TVM_CC_TCCLIB 0)
endif()

if(WIN32 OR NOT PSI_TVM_LLVM)
  if (PSI_TVM_CC_TCCLIB)
    set(PSI_TVM_JIT_DEFAULT "tcclib")
  else()
    set(PSI_TVM_JIT_DEFAULT "c")
  endif()
else()
  set(PSI_TVM_JIT_DEFAULT "llvm")
endif()

set(PSI_TVM_JIT ${PSI_TVM_JIT_DEFAULT} CACHE STRING "Default JIT compiler to use")

# Set up useful GCC flags
if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

  # Try to eliminate LLVM compilation flags which we don't need
  string(REGEX REPLACE "-[WOfg][^ ]*" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
endif()

# Have to ensure _GLIBCXX_DEBUG is consistently defined everywhere otherwise the binary inteface may break
if (LLVM_CXX_FLAGS MATCHES "-D_GLIBCXX_DEBUG")
  message(SEND_ERROR "Please compile LLVM without _GLIBCXX_DEBUG defined")
endif()

# Locate Boost
if(WIN32)
  set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost 1.36.0 COMPONENTS unit_test_framework REQUIRED)
find_package(BISON REQUIRED)

add_custom_target(doc)

find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(Doxyfile.in Doxyfile)
  # This glob is a hack to prevent Doxygen rebuilding all the time
  # It should be mostly okay because when any source files are added some CMakeLists.txt must be updated
  file(GLOB_RECURSE DOXYGEN_INPUTS ${CMAKE_CURRENT_SOURCE_DIR}/src *.[ch]pp)
  add_custom_command(OUTPUT doxygen-tags.tag
    COMMAND mkdir -p doc-sphinx
    COMMAND rm -rf doc-sphinx/doxygen
    COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
    COMMAND mv doc-doxygen-tmp/html doc-sphinx/doxygen
    COMMAND rmdir doc-doxygen-tmp
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile ${DOXYGEN_INPUTS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_target(doc-doxygen DEPENDS doxygen-tags.tag)
  add_dependencies(doc doc-doxygen)
endif()

find_program(SPHINX_BUILD NAMES sphinx-build)
if(SPHINX_BUILD)
  configure_file(doc-sphinx/conf.py conf.py)
  add_custom_target(doc-sphinx ${SPHINX_BUILD} -c . ${CMAKE_CURRENT_SOURCE_DIR}/doc-sphinx doc-sphinx)
  add_dependencies(doc doc-sphinx)
  add_custom_target(doc-sphinx-epub ${SPHINX_BUILD} -b epub -c . ${CMAKE_CURRENT_SOURCE_DIR}/doc-sphinx doc-sphinx-epub)
  add_dependencies(doc doc-sphinx-epub)
  
  if(DOXYGEN_FOUND)
    add_dependencies(doc-sphinx doc-doxygen)
  endif()
  
  find_program(TAR NAMES tar)

  add_custom_target(doc-tar ${TAR} -cjf doc.tar.bz2 --exclude=.* doc-sphinx)
  add_dependencies(doc-tar doc-sphinx)
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
