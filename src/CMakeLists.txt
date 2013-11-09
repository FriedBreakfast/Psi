include_directories(${BOOST_INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
if(PSI_HAVE_TCC)
  include_directories(${TCC_INCLUDE})
endif()
if(PSI_HAVE_READLINE)
  include_directories(${READLINE_INCLUDE})
endif()

configure_file(Config.h.in Config.h)

if(NOT PSI_COMBINE_LIBRARIES)
  function(psi_library var name)
    set(${var} ${name} PARENT_SCOPE)
    set(${var}_SOURCES ${ARGN} PARENT_SCOPE)
    add_library(${name} SHARED ${ARGN})
    shared_library_setup(${name})
  endfunction()
  
  function(psi_library_link_targets name)
    target_link_libraries(${${name}} ${ARGN})
  endfunction()
  
  function(psi_library_link name)
    target_link_libraries(${${name}} ${ARGN})
  endfunction()
else()
  function(psi_library var name)
    set(${var} psi-combined PARENT_SCOPE)
    set(${var}_SOURCES ${ARGN} PARENT_SCOPE)
    set(PSI_COMBINED_SOURCES ${PSI_COMBINED_SOURCES} ${ARGN} PARENT_SCOPE)
  endfunction()

  function(psi_library_link_targets name)
  endfunction()
  
  function(psi_library_link name)
    set(PSI_COMBINED_LINK ${PSI_COMBINED_LINK} ${ARGN} PARENT_SCOPE)
  endfunction()
endif()

if(NOT PSI_COMBINE_LIBRARIES OR NOT PSI_COMBINE_MODULES)
  function(psi_module var name)
    set(${var} ${name} PARENT_SCOPE)
    set(${var}_SOURCES ${ARGN} PARENT_SCOPE)
    add_library(${name} MODULE ${ARGN})
    shared_library_setup(${name})
  endfunction()

  function(psi_module_link_targets name)
    target_link_libraries(${${name}} ${ARGN})
  endfunction()
  
  function(psi_module_link name)
    target_link_libraries(${${name}} ${ARGN})
  endfunction()
else()
  function(psi_module var name)
    set(${var} ${name} PARENT_SCOPE)
    set(${var}_SOURCES ${ARGN} PARENT_SCOPE)
    set(PSI_COMBINED_SOURCES ${PSI_COMBINED_SOURCES} ${ARGN} PARENT_SCOPE)
  endfunction()

  function(psi_module_link_targets name)
  endfunction()
  
  function(psi_module_link name)
    set(PSI_COMBINED_LINK ${PSI_COMBINED_LINK} ${ARGN} PARENT_SCOPE)
  endfunction()
endif()

if(PSI_DEBUG)
psi_library(PSI_ASSERT_LIB psi-assert Assert.cpp Assert.hpp)
endif()

if(BUILD_TESTING)
  if(PSI_WITH_CMDLINE)
    set(PSI_TEST_EXTRA Test/TestConsole.cpp OptionParser.cpp OptionParser.hpp)
  elseif(ANDROID)
    set(PSI_TEST_EXTRA Test/TestAndroid.cpp)
  endif()
  psi_library(PSI_TEST_LIB psi-test Test/Test.cpp Test/Test.hpp ${PSI_TEST_EXTRA})
  psi_library_link_targets(PSI_TEST_LIB ${PSI_ASSERT_LIB})

  function(psi_test_component name)
    if(PSI_WITH_CMDLINE)
      add_executable(${name} Test/TestMain.cpp ${ARGN})
    else()
      add_library(${name} SHARED ${ARGN})
      shared_library_setup(${name})
    endif()
    target_link_libraries(${name} ${PSI_TEST_LIB})
  endfunction()
endif()

if(WIN32)
set(PSI_COMPILER_COMMON_SOURCES Platform/PlatformWindows.cpp Platform/PlatformWindows.hpp Platform/PlatformImplWindows.hpp)
set(PSI_COMPILER_COMMON_EXTRA_LIBS shlwapi)
set(PSI_COMPILER_SOURCES Platform/PlatformCompileWindows.cpp)
set(PSI_TVM_JIT_SOURCES Tvm/JitWindows.cpp)
elseif(UNIX)
if (CMAKE_SYSTEM MATCHES "Linux")
set(PSI_COMPILER_COMMON_UNIX_EXTRA Platform/PlatformLinux.cpp)
else(CMAKE_SYSTEM MATCHES "BSD")
set(PSI_COMPILER_COMMON_UNIX_EXTRA Platform/PlatformBSD.cpp)
endif()

set(PSI_COMPILER_COMMON_SOURCES Platform/PlatformUnix.cpp Platform/PlatformUnix.hpp Platform/PlatformImplUnix.hpp ${PSI_COMPILER_COMMON_UNIX_EXTRA})
set(PSI_COMPILER_SOURCES Platform/PlatformCompileUnix.cpp)
set(PSI_TVM_JIT_SOURCES Tvm/JitLinux.cpp)
set(PSI_RUNTIME_SOURCES Runtime/ExceptionLinux.c Runtime/ExceptionLinuxABI.h)
endif()

if(PSI_TVM_JIT_STATIC)
  # Overrides platform settings
  set(PSI_TVM_JIT_SOURCES Tvm/JitStatic.cpp)
endif()

psi_library(PSI_COMPILER_COMMON_LIB psi-compiler-common
  Configuration.cpp Configuration.hpp
  CppCompiler.hpp
  ErrorContext.cpp ErrorContext.hpp
  Export.hpp
  Lexer.cpp Lexer.hpp
  Platform/Platform.cpp Platform/Platform.hpp
  PropertyValue.cpp PropertyValue.hpp
  Runtime.cpp Runtime.hpp
  SourceLocation.cpp SourceLocation.hpp
  Utility.cpp Utility.hpp
  ${PSI_COMPILER_COMMON_SOURCES}
)

psi_library_link(PSI_COMPILER_COMMON_LIB ${PSI_COMPILER_COMMON_EXTRA_LIBS})
psi_library_link_targets(PSI_COMPILER_COMMON_LIB ${PSI_ASSERT_LIB})

psi_library(PSI_TVM_LIB psi-tvm
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
  ${PSI_TVM_JIT_SOURCES}
)

psi_library_link(PSI_TVM_LIB ${CMAKE_DL_LIBS})
psi_library_link_targets(PSI_TVM_LIB ${PSI_ASSERT_LIB} ${PSI_COMPILER_COMMON_LIB})

if(PSI_HAVE_LLVM)
  psi_module(PSI_TVM_LLVM_LIB psi-tvm-llvm
    Tvm/llvm/Builder.cpp Tvm/llvm/Builder.hpp
    Tvm/llvm/CallingConventions.cpp Tvm/llvm/CallingConventions.hpp
    Tvm/llvm/Engine.cpp Tvm/llvm/Engine.hpp
    Tvm/llvm/Function.cpp
    Tvm/llvm/FunctionalConstant.cpp
    Tvm/llvm/FunctionalInstruction.cpp
    Tvm/llvm/Instructions.cpp
    Tvm/llvm/JIT.cpp
    Tvm/llvm/LLVMPushWarnings.hpp Tvm/llvm/LLVMPopWarnings.hpp
    Tvm/llvm/Target.cpp
    Tvm/llvm/Type.cpp
  )

  psi_module_link(PSI_TVM_LLVM_LIB ${LLVM_LIBS} ${LLVM_LDFLAGS})
  psi_module_link_targets(PSI_TVM_LLVM_LIB ${PSI_TVM_LIB} ${PSI_ASSERT_LIB})

  set_source_files_properties(${PSI_TVM_LLVM_LIB_SOURCES} PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}")
  set_source_files_properties(Tvm/llvm/Engine.cpp PROPERTIES COMPILE_FLAGS "${LLVM_CXX_COMPAT_FLAGS}")
endif()

if(PSI_TVM_C)
  psi_module(PSI_TVM_C_LIB psi-tvm-c
    Tvm/c-backend/Builder.cpp Tvm/c-backend/Builder.hpp
    Tvm/c-backend/CCompiler.cpp
    Tvm/c-backend/CModule.cpp Tvm/c-backend/CModule.hpp Tvm/c-backend/COperators.hpp
    Tvm/c-backend/ValueBuilder.cpp
    Tvm/c-backend/TypeBuilder.cpp
    ${PSI_TVM_CC_SOURCES}
  )
  psi_module_link_targets(PSI_TVM_C_LIB ${PSI_TVM_LIB} ${PSI_ASSERT_LIB})
endif()

psi_library(PSI_RUNTIME_LIB psi-runtime
Runtime/StackAlloc.c
${PSI_RUNTIME_SOURCES}
)

psi_library(PSI_COMPILER_LIB psi-compiler
  Aggregate.cpp Aggregate.hpp
  Array.hpp
  BuiltinTypes.cpp
  Compiler.cpp Compiler.hpp
  ExpressionCompiler.cpp
  Function.cpp
  Implementation.cpp Implementation.hpp
  Interface.cpp Interface.hpp
  GarbageCollection.hpp
  Macros.cpp Macros.hpp
  Parser.cpp Parser.hpp
  Platform/PlatformCompile.cpp Platform/PlatformCompile.hpp
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

psi_library_link(PSI_COMPILER_LIB ${CMAKE_DL_LIBS})
psi_library_link_targets(PSI_COMPILER_LIB ${PSI_COMPILER_COMMON_LIB} ${PSI_TVM_LIB} ${PSI_ASSERT_LIB})

if(PSI_COMBINE_LIBRARIES)
  add_library(psi-combined SHARED ${PSI_COMBINED_SOURCES})
  shared_library_setup(psi-combined)
  target_link_libraries(psi-combined ${PSI_COMBINED_LINK})
endif()

if (PSI_WITH_CMDLINE)
  add_executable(psi
  Main.cpp
  OptionParser.cpp OptionParser.hpp
  )
  target_link_libraries(psi ${PSI_COMPILER_LIB})
  if(PSI_HAVE_READLINE)
    target_link_libraries(psi ${READLINE_LIB})
  endif()
endif()

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)

if(BUILD_TESTING)
  psi_test_component(psi-tvm-test
    Tvm/Test.cpp Tvm/Test.hpp
    Tvm/AggregateTest.cpp
    Tvm/InstructionTest.cpp
    Tvm/DerivedTest.cpp
    Tvm/FunctionTest.cpp
    Tvm/MemoryTest.cpp
    Tvm/NumberTest.cpp
    Tvm/ParserTest.cpp
  )

  target_link_libraries(psi-tvm-test ${PSI_TVM_LIB} ${PSI_TEST_LIB} ${PSI_ASSERT_LIB})
  if(PSI_TVM_JIT STREQUAL "llvm")
    add_dependencies(psi-tvm-test psi-tvm-llvm)
  elseif(PSI_TVM_JIT STREQUAL "c")
    add_dependencies(psi-tvm-test psi-tvm-c)
  endif()
  
  macro(add_tvm_test name config)
    add_test(NAME psi-tvm-test-${name} COMMAND psi-tvm-test)
    set_property(TEST psi-tvm-test-${name} PROPERTY ENVIRONMENT "PSI_CONFIG_EXTRA=${config}")
  endmacro()

  if(PSI_HAVE_LLVM)
    add_tvm_test(llvm "tvm.jit=\"llvm\"")
  endif()
  if(PSI_TVM_C)
    add_tvm_test(cc "tvm.jit=\"cc\"")
    if(PSI_HAVE_TCC)
      target_link_libraries(psi-tvm-c ${TCC_LIB})
      add_tvm_test(tcc "tvm.jit=\"tcclib\"")
    endif()
  endif()
else()
  macro(add_tvm_test name config)
  endmacro()
endif()
