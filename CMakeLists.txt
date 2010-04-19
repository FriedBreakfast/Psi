cmake_minimum_required(VERSION 2.6)
project(type-test)

if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -Wall")
endif()

find_package(Boost 1.36.0 COMPONENTS unit_test_framework)

if(NOT Boost_FOUND)
  message(SEND_ERROR "Boost not found")
endif()

add_subdirectory(src)
