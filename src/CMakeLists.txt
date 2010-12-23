include_directories(${Boost_INCLUDE_DIRS} ${GMP_INCLUDE_DIRECTORIES})

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp)
set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

bison_target(TvmParser Tvm/Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated.cpp VERBOSE ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated)
set_source_files_properties(${BISON_TvmParser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/Tvm")

add_library(psi SHARED
  Utility.cpp Utility.hpp

  ${BISON_Parser_OUTPUTS} Parser.hpp

  Tvm/Aggregate.cpp Tvm/Aggregate.hpp
  Tvm/Assembler.cpp Tvm/Assembler.hpp
  Tvm/AssemblerOperations.cpp
  Tvm/Core.cpp Tvm/Core.hpp
  Tvm/Function.cpp Tvm/Function.hpp
  Tvm/Functional.cpp Tvm/Functional.hpp
  Tvm/Jit.hpp
  Tvm/Instructions.cpp Tvm/Instructions.hpp
  Tvm/Number.cpp Tvm/Number.hpp
  ${BISON_TvmParser_OUTPUTS} Tvm/Parser.hpp
  Tvm/Recursive.cpp Tvm/Recursive.hpp
  Tvm/Rewrite.hpp
  Tvm/User.cpp Tvm/User.hpp
  Tvm/Utility.cpp Tvm/Utility.hpp

  Tvm/LLVM/Builder.cpp Tvm/LLVM/Builder.hpp
  Tvm/LLVM/Aggregate.cpp
  Tvm/LLVM/Function.cpp
  Tvm/LLVM/Instructions.cpp
  Tvm/LLVM/Primitive.cpp
  Tvm/LLVM/Number.cpp
  Tvm/LLVM/Templates.hpp
)

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)

#foreach(dir "." "Tvm")
#  message(STATUS ${dir})
#  file(GLOB headers ${dir}/*.hpp)
#  install(FILES ${headers} DESTINATION include/${dir})
#endforeach()

set_target_properties(psi PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(psi ${LLVM_LIBS})

#add_library(typetest SHARED
#  ExpressionCompiler.cpp ExpressionCompiler.hpp
#  TypeSystem.cpp TypeSystem.hpp
#  ${BISON_Parser_OUTPUTS} Parser.hpp
#  Utility.cpp Utility.hpp
#  Maybe.hpp
#  Variant.hpp)

#set_target_properties(typetest PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")

#add_executable(type-test TypeTest.cpp)

add_executable(test TestMain.cpp
  Tvm/Test.cpp Tvm/Test.hpp
  Tvm/AggregateTest.cpp
  Tvm/InstructionTest.cpp
  Tvm/DerivedTest.cpp
  Tvm/FunctionTest.cpp
  Tvm/NumberTest.cpp
  Tvm/ParserTest.cpp
#  MaybeTest.cpp
#  TypeSystemTest.cpp
#  VariantTest.cpp
  )

set_target_properties(test PROPERTIES
  COMPILE_DEFINITIONS "BOOST_TEST_DYN_LINK"
  COMPILE_FLAGS "${LLVM_CXX_FLAGS}"
  LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} "psi")
