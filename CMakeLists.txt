cmake_minimum_required(VERSION 2.6)
project(type-test)

# Locate LLVM
find_program(LLVM_CONFIG NAMES llvm-config)

if(${LLVM_CONFIG} STREQUAL "LLVM_CONFIG-NOTFOUND")
  message(SEND_ERROR "LLVM not found")
endif()

execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs core OUTPUT_VARIABLE LLVM_LIBS_CORE OUTPUT_STRIP_TRAILING_WHITESPACE)

# Set up useful GCC flags
if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -Wall -Wextra")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
  # I need exception handling; not sure whether this will cause problems later though
  set(LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS} -fexceptions")
endif()

# Locate Boost (only required for unit tests)
find_package(Boost 1.36.0 COMPONENTS unit_test_framework)
if(NOT Boost_FOUND)
  message(SEND_ERROR "Boost not found")
endif()

find_package(BISON)
if(NOT BISON_FOUND)
  message(SEND_ERROR "Bison not found")
endif()

find_package(Doxygen)

add_subdirectory(src)
