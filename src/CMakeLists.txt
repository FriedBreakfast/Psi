include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

configure_file(Config.h.in Config.h)

add_library(psi-assert SHARED Assert.cpp Assert.hpp)

bison_target(TvmParser Tvm/Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated.cpp VERBOSE ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated)
set_source_files_properties(${BISON_TvmParser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/Tvm")

add_library(psi-compiler-common SHARED
  Runtime.cpp Runtime.hpp
  SourceLocation.cpp SourceLocation.hpp
)

add_library(psi-tvm SHARED
  Tvm/Aggregate.cpp Tvm/Aggregate.hpp
  Tvm/AggregateLowering.cpp
  Tvm/Assembler.cpp Tvm/Assembler.hpp
  Tvm/AssemblerOperations.cpp
  Tvm/BigInteger.cpp Tvm/BigInteger.hpp
  Tvm/Core.cpp Tvm/Core.hpp
  Tvm/Disassembler.cpp
  Tvm/Function.cpp Tvm/Function.hpp
  Tvm/Functional.cpp Tvm/Functional.hpp
  Tvm/FunctionalBuilder.cpp Tvm/FunctionalBuilder.hpp
  Tvm/Jit.cpp Tvm/Jit.hpp
  Tvm/Instructions.cpp Tvm/Instructions.hpp
  Tvm/InstructionBuilder.cpp Tvm/InstructionBuilder.hpp
  Tvm/ModuleRewriter.cpp Tvm/ModuleRewriter.hpp
  Tvm/Number.cpp Tvm/Number.hpp
  ${BISON_TvmParser_OUTPUTS} Tvm/Parser.hpp
  Tvm/Recursive.cpp Tvm/Recursive.hpp
  Tvm/Source.cpp
  Tvm/TermOperationMap.hpp
  Tvm/Utility.hpp
)

target_link_libraries(psi-tvm psi-compiler-common debug psi-assert)
if(UNIX)
  target_link_libraries(psi-tvm -ldl)
endif()

if(BUILD_TESTING)
  add_executable(psi-tvm-test
    Tvm/Test.cpp Tvm/Test.hpp
    Tvm/AggregateTest.cpp
    Tvm/InstructionTest.cpp
    Tvm/DerivedTest.cpp
    Tvm/FunctionTest.cpp
    Tvm/NumberTest.cpp
    Tvm/ParserTest.cpp
  )

  set_target_properties(psi-tvm-test PROPERTIES COMPILE_DEFINITIONS "BOOST_TEST_DYN_LINK")
  target_link_libraries(psi-tvm-test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} psi-tvm debug psi-assert)

  add_test(psi-tvm-test psi-tvm-test)
endif()

add_library(psi-tvm-llvm MODULE
  Tvm/llvm/Builder.cpp Tvm/llvm/Builder.hpp
  Tvm/llvm/Function.cpp
  Tvm/llvm/FunctionalConstant.cpp
  Tvm/llvm/FunctionalInstruction.cpp
  Tvm/llvm/Instructions.cpp
  Tvm/llvm/Target.cpp Tvm/llvm/Target.hpp
  Tvm/llvm/Target_AMD64.cpp
  Tvm/llvm/Templates.hpp
  Tvm/llvm/Type.cpp
)

set_target_properties(psi-tvm-llvm PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(psi-tvm-llvm ${LLVM_LIBS} debug psi-assert)

if(UNIX)
list(APPEND PSI_RUNTIME_SOURCES Runtime/ExceptionLinux.c Runtime/ExceptionLinuxABI.h)
list(APPEND PSI_COMPILER_SOURCES PlatformLinux.cpp)
elseif(WIN32)
list(APPEND PSI_COMPILER_SOURCES PlatformWindows.cpp)
endif()

add_library(psi-runtime SHARED
  ${PSI_RUNTIME_SOURCES}
)

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp)
set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

add_library(psi-compiler SHARED
  Class.cpp Class.hpp
  CppCompiler.hpp
  Compiler.cpp Compiler.hpp
  ExpressionCompiler.cpp
  Function.cpp Function.hpp
  Interface.cpp
  FunctionLower.cpp FunctionLower.hpp
  GarbageCollection.hpp
  Macros.cpp
  ${BISON_Parser_OUTPUTS} Parser.hpp
  Platform.cpp Platform.hpp
  Tree.cpp Tree.hpp
  TreePattern.hpp
  Utility.cpp Utility.hpp
  Visitor.hpp
  ${PSI_COMPILER_SOURCES}
)

target_link_libraries(psi-compiler psi-compiler-common debug psi-assert)
if (UNIX)
  target_link_libraries(psi-compiler -ldl)
endif()

set_target_properties(psi-tvm-llvm PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(psi-tvm-llvm ${LLVM_LIBS} debug psi-assert)

add_executable(psi Main.cpp)
target_link_libraries(psi psi-compiler psi-tvm debug psi-assert)

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)
