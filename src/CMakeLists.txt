include_directories(${Boost_INCLUDE_DIRS})

add_library(typetest SHARED
  CodeGenerator.cpp CodeGenerator.hpp
  TypeSystem.cpp TypeSystem.hpp
  Maybe.hpp
  Variant.hpp)

set_target_properties(typetest PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(typetest ${LLVM_LIBS_CORE})

add_executable(type-test TypeTest.cpp)

add_executable(test TestMain.cpp
  MaybeTest.cpp
  TypeSystemTest.cpp
  VariantTest.cpp)

set_target_properties(test PROPERTIES COMPILE_DEFINITIONS "BOOST_TEST_DYN_LINK")
target_link_libraries(test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} "typetest")