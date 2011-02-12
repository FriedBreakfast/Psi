cmake_minimum_required(VERSION 2.6)
project(type-test)

include(CTest)

# Locate LLVM
find_program(LLVM_CONFIG NAMES llvm-config)

if(${LLVM_CONFIG} STREQUAL "LLVM_CONFIG-NOTFOUND")
  message(SEND_ERROR "LLVM not found")
endif()

execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_CXX_FLAGS_DEBUG "-DPSI_DEBUG=1")

# Set up useful GCC flags
if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g3")
  # I need exception handling; not sure whether this will cause problems later though
  string(REGEX REPLACE "-fno-exceptions" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
  # This should show up as a link time error if there is a problem
  string(REGEX REPLACE "-fno-rtti" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
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
find_package(Boost 1.36.0 COMPONENTS unit_test_framework)
if(NOT Boost_FOUND)
  message(SEND_ERROR "Boost not found")
endif()

find_package(BISON)
if(NOT BISON_FOUND)
  message(SEND_ERROR "Bison not found")
endif()

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
