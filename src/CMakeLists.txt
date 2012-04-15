include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

configure_file(Config.h.in Config.h)

bison_target(TvmParser Tvm/Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated.cpp VERBOSE ${CMAKE_CURRENT_BINARY_DIR}/TvmParserGenerated)
set_source_files_properties(${BISON_TvmParser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/Tvm")

add_library(psi-assert SHARED Assert.cpp Assert.hpp)

bison_target(Parser Parser.ypp ${CMAKE_CURRENT_BINARY_DIR}/ParserGenerated.cpp)
set_source_files_properties(${BISON_Parser_OUTPUTS} PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}")

if(UNIX)
list(APPEND PSI_RUNTIME_SOURCES Runtime/ExceptionLinux.c Runtime/ExceptionLinuxABI.h)
list(APPEND PSI_COMPILER_SOURCES PlatformLinux.cpp)
elseif(WIN32)
list(APPEND PSI_COMPILER_SOURCES PlatformWindows.cpp)
endif()

add_library(psi-runtime SHARED
  ${PSI_RUNTIME_SOURCES}
)

add_library(psi-compiler SHARED
  Class.cpp Class.hpp
  CppCompiler.hpp
  Compiler.cpp Compiler.hpp
  ExpressionCompiler.cpp
  Function.cpp Function.hpp
  Interface.cpp
  GarbageCollection.hpp
  Macros.cpp
  ${BISON_Parser_OUTPUTS} Parser.hpp
  Platform.cpp Platform.hpp
  Runtime.cpp Runtime.hpp
  Tree.cpp Tree.hpp
  TreePattern.hpp
  Utility.cpp Utility.hpp
  Visitor.hpp
  ${PSI_COMPILER_SOURCES}
)

target_link_libraries(psi-compiler debug psi-assert)
if (UNIX)
  target_link_libraries(psi-compiler -ldl)
endif()

add_executable(psi Main.cpp)

target_link_libraries(psi psi-compiler debug psi-assert)

#install(TARGETS psi
#  LIBRARY DESTINATION lib
#  PUBLIC_HEADER DESTINATION include/Psi)
