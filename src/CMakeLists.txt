include_directories(${Boost_INCLUDE_DIRS})

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp)

set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

add_library(typetest SHARED
  CodeGenerator.cpp CodeGenerator.hpp
  ExpressionCompiler.cpp ExpressionCompiler.hpp
  TypeSystem.cpp TypeSystem.hpp
  ${BISON_Parser_OUTPUTS} Parser.hpp
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