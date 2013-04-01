include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR} ${JSONC_INCLUDE})

configure_file(Config.h.in Config.h)

add_library(psi-assert SHARED Assert.cpp Assert.hpp)

bison_target(TvmParser Tvm/Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated.cpp VERBOSE ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated)
set_source_files_properties(${BISON_TvmParser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/Tvm")

if(WIN32)
set(PSI_COMPILER_COMMON_SOURCES PlatformWindows.cpp PlatformWindows.hpp)
set(PSI_COMPILER_SOURCES PlatformCompileWindows.cpp)
set(PSI_TVM_SOURCES Tvm/JitWindows.cpp)
elseif(UNIX)
set(PSI_COMPILER_COMMON_SOURCES PlatformLinux.cpp PlatformLinux.hpp)
set(PSI_COMPILER_SOURCES PlatformCompileLinux.cpp)
set(PSI_TVM_SOURCES Tvm/JitLinux.cpp)
set(PSI_RUNTIME_SOURCES Runtime/ExceptionLinux.c Runtime/ExceptionLinuxABI.h)
endif()

add_library(psi-compiler-common SHARED
  Runtime.cpp Runtime.hpp
  SourceLocation.cpp SourceLocation.hpp
  Utility.cpp Utility.hpp
  ErrorContext.cpp ErrorContext.hpp
  Export.hpp
  CppCompiler.hpp
  Platform.cpp Platform.hpp
  ${PSI_COMPILER_COMMON_SOURCES}
)

target_link_libraries(psi-compiler-common psi-assert ${JSONC_LIB})

add_library(psi-tvm SHARED
  Tvm/Aggregate.cpp Tvm/Aggregate.hpp
  Tvm/AggregateLowering.cpp Tvm/AggregateLowering.hpp
  Tvm/AggregateLoweringOperations.cpp
  Tvm/Assembler.cpp Tvm/Assembler.hpp
  Tvm/AssemblerOperations.cpp
  Tvm/BigInteger.cpp Tvm/BigInteger.hpp
  Tvm/Core.cpp Tvm/Core.hpp
  Tvm/Disassembler.cpp
  Tvm/DisassemblerSource.cpp
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
  Tvm/TermOperationMap.hpp
  Tvm/Utility.hpp
  Tvm/ValueList.hpp
  ${PSI_TVM_SOURCES}
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
    Tvm/MemoryTest.cpp
    Tvm/NumberTest.cpp
    Tvm/ParserTest.cpp
  )

  if(NOT WIN32)
    set_target_properties(psi-tvm-test PROPERTIES COMPILE_DEFINITIONS "BOOST_TEST_DYN_LINK")
  endif()
  target_link_libraries(psi-tvm-test ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} psi-tvm debug psi-assert)
  add_dependencies(psi-tvm-test psi-tvm-llvm)

  add_test(psi-tvm-test psi-tvm-test)
endif()

add_library(psi-tvm-llvm MODULE
  Tvm/llvm/Builder.cpp Tvm/llvm/Builder.hpp
  Tvm/llvm/Function.cpp
  Tvm/llvm/FunctionalConstant.cpp
  Tvm/llvm/FunctionalInstruction.cpp
  Tvm/llvm/Instructions.cpp
  Tvm/llvm/Target.cpp Tvm/llvm/Target.hpp
  Tvm/llvm/Target_Linux_x86.cpp
  Tvm/llvm/Target_AMD64.cpp
  Tvm/llvm/Target_Win32.cpp
  Tvm/llvm/Type.cpp
)

set_target_properties(psi-tvm-llvm PROPERTIES COMPILE_FLAGS ${LLVM_CXX_FLAGS})
target_link_libraries(psi-tvm-llvm psi-tvm ${LLVM_LIBS} debug psi-assert ${LLVM_LDFLAGS})

add_library(psi-tvm-c MODULE
  Tvm/c-backend/Builder.cpp
  Tvm/c-backend/CCompiler.cpp
  Tvm/c-backend/CModule.cpp Tvm/c-backend/CModule.hpp Tvm/c-backend/COperators.def
  Tvm/c-backend/ValueBuilder.cpp
  Tvm/c-backend/TypeBuilder.cpp
)

add_library(psi-runtime SHARED
Runtime/StackAlloc.c
${PSI_RUNTIME_SOURCES}
)

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp VERBOSE ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated)
set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

add_library(psi-compiler SHARED
  Aggregate.cpp
  Array.hpp
  BuiltinTypes.cpp
  Compiler.cpp Compiler.hpp
  ExpressionCompiler.cpp
  Function.cpp
  Interface.cpp
  GarbageCollection.hpp
  Macros.cpp Macros.hpp
  ${BISON_Parser_OUTPUTS} Parser.hpp
  PlatformCompile.cpp PlatformCompile.hpp
  StaticDispatch.cpp StaticDispatch.hpp
  Term.cpp Term.hpp
  TermBuilder.cpp TermBuilder.hpp
  TopologicalSort.hpp
  Tree.cpp Tree.hpp
  TreeBase.cpp TreeBase.hpp
  TvmFunctionalLowering.cpp
  TvmFunctionLowering.cpp TvmFunctionLowering.hpp
  TvmInstructionLowering.cpp
  TvmLifecycle.cpp
  TvmLowering.cpp TvmLowering.hpp
  Visitor.hpp
  ${PSI_COMPILER_SOURCES}
)

target_link_libraries(psi-compiler psi-compiler-common psi-tvm debug psi-assert)
if (UNIX)
  target_link_libraries(psi-compiler -ldl)
endif()

if (UNIX)
  add_library(psi-compiler-gcchecker SHARED GCChecker.c)
endif()

set_target_properties(psi-tvm-llvm PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(psi-tvm-llvm ${LLVM_LIBS} debug psi-assert)

add_executable(psi Main.cpp)
target_link_libraries(psi psi-compiler)

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)
