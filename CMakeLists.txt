cmake_minimum_required(VERSION 2.6)
project(type-test)

include(CPack)
include(CTest)

set(PSI_DEBUG 0 CACHE BOOL "Enable debug checks")
set(PSI_ENABLE_VALGRIND 0 CACHE BOOL "Enable Valgrind support")

# Locate LLVM
find_program(LLVM_CONFIG NAMES llvm-config)

if(${LLVM_CONFIG} STREQUAL "LLVM_CONFIG-NOTFOUND")
  message(SEND_ERROR "LLVM not found")
endif()

execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

if(LLVM_CXX_FLAGS MATCHES "-fno-exceptions" OR LLVM_CXX_FLAGS MATCHES "-fno-rtti")
  message(SEND_ERROR "LLVM must be built with REQUIRES_EH=1 and REQUIRES_RTTI=1 set")
endif()

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
find_package(Boost 1.36.0 REQUIRED unit_test_framework)
find_package(BISON REQUIRED)

find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
  add_custom_target(doc
    rm -rf doc/html
    COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} VERBATIM)
endif()

enable_testing()

add_subdirectory(src)
