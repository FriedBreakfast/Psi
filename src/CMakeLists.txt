include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
if(TCC_INCLUDE)
  include_directories(${TCC_INCLUDE})
endif()
if(PSI_ENABLE_READLINE)
  include_directories(${READLINE_INCLUDE})
endif()

configure_file(Config.h.in Config.h)

if(PSI_DEBUG)
add_library(psi-assert SHARED Assert.cpp Assert.hpp)
shared_library_setup(psi-assert)
set(PSI_ASSERT_LIB psi-assert)
endif()

add_library(psi-test SHARED Test.cpp Test.hpp OptionParser.cpp OptionParser.hpp)
target_link_libraries(psi-test ${PSI_ASSERT_LIB})
shared_library_setup(psi-test)

if(WIN32)
set(PSI_COMPILER_COMMON_SOURCES PlatformWindows.cpp PlatformWindows.hpp PlatformImplWindows.hpp)
set(PSI_COMPILER_COMMON_LIBS shlwapi)
set(PSI_COMPILER_SOURCES PlatformCompileWindows.cpp)
set(PSI_TVM_SOURCES Tvm/JitWindows.cpp)
elseif(UNIX)
if (CMAKE_SYSTEM MATCHES "Linux")
set(PSI_COMPILER_COMMON_UNIX_EXTRA PlatformLinux.cpp)
else(CMAKE_SYSTEM MATCHES "BSD")
set(PSI_COMPILER_COMMON_UNIX_EXTRA PlatformBSD.cpp)
endif()

set(PSI_COMPILER_COMMON_SOURCES PlatformUnix.cpp PlatformUnix.hpp PlatformImplUnix.hpp ${PSI_COMPILER_COMMON_UNIX_EXTRA})
set(PSI_COMPILER_SOURCES PlatformCompileUnix.cpp)
set(PSI_TVM_SOURCES Tvm/JitLinux.cpp)
set(PSI_RUNTIME_SOURCES Runtime/ExceptionLinux.c Runtime/ExceptionLinuxABI.h)
endif()

add_library(psi-compiler-common SHARED
  Configuration.cpp Configuration.hpp
  CppCompiler.hpp
  ErrorContext.cpp ErrorContext.hpp
  Export.hpp
  Lexer.cpp Lexer.hpp
  Platform.cpp Platform.hpp
  PropertyValue.cpp PropertyValue.hpp
  Runtime.cpp Runtime.hpp
  SourceLocation.cpp SourceLocation.hpp
  Utility.cpp Utility.hpp
  ${PSI_COMPILER_COMMON_SOURCES}
)
shared_library_setup(psi-compiler-common)

target_link_libraries(psi-compiler-common ${PSI_ASSERT_LIB} ${PSI_COMPILER_COMMON_LIBS})

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
  Tvm/Parser.cpp Tvm/Parser.hpp
  Tvm/Recursive.cpp Tvm/Recursive.hpp
  Tvm/TermOperationMap.hpp
  Tvm/Utility.hpp
  Tvm/ValueList.hpp
  ${PSI_TVM_SOURCES}
)
shared_library_setup(psi-tvm)

target_link_libraries(psi-tvm psi-compiler-common ${PSI_ASSERT_LIB} ${CMAKE_DL_LIBS})

if(BUILD_TESTING)
  add_executable(psi-tvm-test
    TestMain.cpp
    Tvm/Test.cpp Tvm/Test.hpp
    Tvm/AggregateTest.cpp
    Tvm/InstructionTest.cpp
    Tvm/DerivedTest.cpp
    Tvm/FunctionTest.cpp
    Tvm/MemoryTest.cpp
    Tvm/NumberTest.cpp
    Tvm/ParserTest.cpp
  )

  target_link_libraries(psi-tvm-test psi-tvm psi-test ${PSI_ASSERT_LIB})
  if(PSI_TVM_JIT STREQUAL "llvm")
    add_dependencies(psi-tvm-test psi-tvm-llvm)
  elseif(PSI_TVM_JIT STREQUAL "c")
    add_dependencies(psi-tvm-test psi-tvm-c)
  endif()
  
  macro(add_tvm_test name config)
    add_test(NAME psi-tvm-test-${name} COMMAND psi-tvm-test)
    set_property(TEST psi-tvm-test-${name} PROPERTY ENVIRONMENT "PSI_CONFIG_EXTRA=${config}")
  endmacro()
else()
  macro(add_tvm_test name config)
  endmacro()
endif()

if(PSI_TVM_LLVM)
  add_library(psi-tvm-llvm MODULE
    Tvm/llvm/Builder.cpp Tvm/llvm/Builder.hpp
    Tvm/llvm/CallingConventions.cpp Tvm/llvm/CallingConventions.hpp
    Tvm/llvm/Function.cpp
    Tvm/llvm/FunctionalConstant.cpp
    Tvm/llvm/FunctionalInstruction.cpp
    Tvm/llvm/Instructions.cpp
    Tvm/llvm/LLVMPushWarnings.hpp Tvm/llvm/LLVMPopWarnings.hpp
    Tvm/llvm/Target.cpp
    Tvm/llvm/Type.cpp
  )
  shared_library_setup(psi-tvm-llvm)

  set_target_properties(psi-tvm-llvm PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}")
  target_link_libraries(psi-tvm-llvm psi-tvm ${LLVM_LIBS} ${LLVM_LDFLAGS} ${PSI_ASSERT_LIB})
  add_tvm_test(llvm "tvm.jit=\"llvm\"")
endif()

add_library(psi-tvm-c MODULE
  Tvm/c-backend/Builder.cpp Tvm/c-backend/Builder.hpp
  Tvm/c-backend/CCompiler.cpp
  Tvm/c-backend/CModule.cpp Tvm/c-backend/CModule.hpp Tvm/c-backend/COperators.hpp
  Tvm/c-backend/ValueBuilder.cpp
  Tvm/c-backend/TypeBuilder.cpp
  ${PSI_TVM_CC_SOURCES}
)
shared_library_setup(psi-tvm-c)

add_tvm_test(cc "tvm.jit=\"cc\"")

target_link_libraries(psi-tvm-c psi-tvm ${PSI_ASSERT_LIB})
if(PSI_TVM_CC_TCCLIB)
  target_link_libraries(psi-tvm-c ${TCC_LIB})
  add_tvm_test(tcc "tvm.jit=\"tcclib\"")
endif()

add_library(psi-runtime SHARED
Runtime/StackAlloc.c
${PSI_RUNTIME_SOURCES}
)

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
  Parser.cpp Parser.hpp
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
  TvmSymbolNaming.cpp
  Visitor.hpp
  ${PSI_COMPILER_SOURCES}
)
shared_library_setup(psi-compiler)

target_link_libraries(psi-compiler psi-compiler-common psi-tvm ${PSI_ASSERT_LIB} ${CMAKE_DL_LIBS})

if (CMAKE_SYSTEM MATCHES "Linux")
  add_library(psi-compiler-gcchecker SHARED GCChecker.c)
endif()

add_executable(psi
Main.cpp
OptionParser.cpp OptionParser.hpp
)
target_link_libraries(psi psi-compiler)
if(PSI_ENABLE_READLINE)
  target_link_libraries(psi ${READLINE_LIB})
endif()

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)
