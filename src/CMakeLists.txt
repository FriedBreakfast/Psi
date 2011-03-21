include_directories(${Boost_INCLUDE_DIRS})

bison_target(TvmParser Tvm/Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated.cpp VERBOSE ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated)
set_source_files_properties(${BISON_TvmParser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/Tvm")

add_library(psi-assert SHARED Assert.cpp Assert.hpp)

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
  Tvm/Rewrite.hpp
  Tvm/Source.cpp
  Tvm/TermOperationMap.hpp
  Tvm/User.cpp Tvm/User.hpp
  Tvm/Utility.hpp
)

target_link_libraries(psi-tvm ${PSI_DEBUG_LIBS})
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

add_library(psi-llvm MODULE
  Tvm/LLVM/Builder.cpp Tvm/LLVM/Builder.hpp
  Tvm/LLVM/Function.cpp
  Tvm/LLVM/FunctionalConstant.cpp
  Tvm/LLVM/FunctionalInstruction.cpp
  Tvm/LLVM/Instructions.cpp
  Tvm/LLVM/Target.cpp Tvm/LLVM/Target.hpp
  Tvm/LLVM/Target_AMD64.cpp
  Tvm/LLVM/Templates.hpp
  Tvm/LLVM/Type.cpp
)

set_target_properties(psi-llvm PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS}" LINK_FLAGS "${LLVM_LDFLAGS}")
target_link_libraries(psi-llvm ${LLVM_LIBS} debug psi-assert)

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp)
set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
list(APPEND PSI_RUNTIME_SOURCES Runtime/ExceptionLinux.c Runtime/ExceptionLinuxABI.h)
endif()

add_library(psi-runtime SHARED
  ${PSI_RUNTIME_SOURCES}
)

add_library(psi SHARED
  Compiler.hpp
  Utility.cpp Utility.hpp
  ${BISON_Parser_OUTPUTS} Parser.hpp
)

target_link_libraries(psi psi-tvm debug psi-assert)

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)
