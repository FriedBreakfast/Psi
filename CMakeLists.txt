cmake_minimum_required(VERSION 2.6)
project(type-test)

include(CPack)
include(CTest)

if(WIN32)
  set(PSI_TVM_JIT_DEFAULT "c")
else()
  set(PSI_TVM_JIT_DEFAULT "llvm")
endif()

set(PSI_DEBUG 0 CACHE BOOL "Enable debug checks")
set(PSI_ENABLE_VALGRIND 0 CACHE BOOL "Enable Valgrind support")
set(PSI_TVM_JIT ${PSI_TVM_JIT_DEFAULT} CACHE STRING "Default JIT compiler to use")
set(PSI_OBJECT_PTR_DEBUG 0 CACHE BOOL "Enable object pointer debug checks (very expensive)")
mark_as_advanced(PSI_OBJECT_PTR_DEBUG)

find_library(JSONC_LIB json)
find_path(JSONC_INCLUDE json_object.h)

# Locate LLVM
if(WIN32)
  set(LLVM_PATH "" CACHE PATH "Root of LLVM install.")
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_PATH}/share/llvm/cmake")
  include(LLVMConfig)
  set(LLVM_CXX_FLAGS -I${LLVM_INCLUDE_DIRS} -D${LLVM_DEFINITIONS})
  set(LLVM_LDFLAGS -LIBPATH:${LLVM_LIBRARY_DIRS})
  llvm_map_components_to_libraries(LLVM_LIBS jit native ipo)
else()
  find_program(LLVM_CONFIG NAMES llvm-config)
  if(NOT LLVM_CONFIG)
    message(SEND_ERROR "LLVM not found")
  else()
    execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${LLVM_CONFIG} --libs OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()

if(LLVM_CXX_FLAGS MATCHES "-fno-exceptions" OR LLVM_CXX_FLAGS MATCHES "-fno-rtti")
  message(SEND_ERROR "LLVM must be built with REQUIRES_EH=1 and REQUIRES_RTTI=1 set\nBuild LLVM using: env REQUIRES_EH=1 REQUIRES_RTTI=1 sh -c './configure && make install'")
endif()

# Find TCC
find_library(TCC_LIB tcc)
find_path(TCC_INCLUDE tcclib.h)
if(TCC_LIB AND TCC_INCLUDE)
  # Whether TCC has been compiled in to the C backend
  set(PSI_TVM_CC_TCCLIB 1)
  set(PSI_TVM_CC_DEFAULT "tcclib")
else()
  # Whether TCC has been compiled in to the C backend
  set(PSI_TVM_CC_TCCLIB 0)
  set(PSI_TVM_CC_DEFAULT ${CMAKE_C_COMPILER})
endif()

set(PSI_TVM_CC ${PSI_TVM_CC_DEFAULT} CACHE STRING "Default path to search for C compiler")

# Set up useful GCC flags
if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  # These flags do not have to be consistent between compilation units
  string(REGEX REPLACE "-Woverloaded-virtual" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
  string(REGEX REPLACE "-O.?" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
  string(REGEX REPLACE "-g.?" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
endif()

# Have to ensure _GLIBCXX_DEBUG is consistently defined everywhere otherwise the binary inteface may break
if (LLVM_CXX_FLAGS MATCHES "-D_GLIBCXX_DEBUG")
  string(REGEX REPLACE "-D_GLIBCXX_DEBUG" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_DEBUG")
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
  
  if(DOXYGEN_FOUND)
    add_dependencies(doc-sphinx doc-doxygen)
  endif()
  
  find_program(TAR NAMES tar)

  add_custom_target(doc-tar ${TAR} -cjf doc.tar.bz2 --exclude=.* doc-sphinx)
  add_dependencies(doc-tar doc-sphinx)
endif()

enable_testing()

add_subdirectory(src)
