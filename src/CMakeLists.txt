include_directories(${Boost_INCLUDE_DIRS} ${GMP_INCLUDE_DIRECTORIES})

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp)

set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

add_library(typetest SHARED
  User.cpp User.hpp
  Core.cpp Core.hpp
  Type.cpp Type.hpp
  Value.cpp Value.hpp
  Instruction.cpp Instruction.hpp

  ExpressionCompiler.cpp ExpressionCompiler.hpp
  TypeSystem.cpp TypeSystem.hpp
  ${BISON_Parser_OUTPUTS} Parser.hpp
  Utility.cpp Utility.hpp
  Maybe.hpp
  Variant.hpp)

set_target_properties(typetest PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(typetest ${LLVM_LIBS_CORE} ${LIB_GMP} ${LIB_GMPXX})

add_executable(type-test TypeTest.cpp)

add_executable(test TestMain.cpp
  CoreTest.cpp
  MaybeTest.cpp
  TypeSystemTest.cpp
  VariantTest.cpp)

set_target_properties(test PROPERTIES
  COMPILE_DEFINITIONS "BOOST_TEST_DYN_LINK"
  COMPILE_FLAGS "${LLVM_CXX_FLAGS}")
target_link_libraries(test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} "typetest")