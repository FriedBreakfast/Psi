cmake_minimum_required(VERSION 2.6)
project(Psi)

include(CPack)
include(CTest)

include(DetectPlatform.cmake)

# Enable -fvisibility=hidden on GCC
include(GenerateExportHeader)
add_compiler_export_flags()

set(PSI_DEBUG 0 CACHE BOOL "Enable debug checks")
set(PSI_ENABLE_VALGRIND 0 CACHE BOOL "Enable Valgrind support")
set(PSI_OBJECT_PTR_DEBUG 0 CACHE BOOL "Enable object pointer debug checks (very expensive)")
mark_as_advanced(PSI_OBJECT_PTR_DEBUG)

# Locate LLVM
set(PSI_LLVM_MODULES engine ipo)
find_program(LLVM_CONFIG NAMES llvm-config)
if(LLVM_CONFIG)
  set(PSI_TVM_LLVM 1)
  execute_process(COMMAND ${LLVM_CONFIG} --version OUTPUT_VARIABLE LLVM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${LLVM_CONFIG} --libs ${PSI_LLVM_MODULES} OUTPUT_VARIABLE LLVM_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  find_package(LLVM)
  if(LLVM_VERSION)
    set(PSI_TVM_LLVM 1)
    set(LLVM_CXX_FLAGS -I${LLVM_INCLUDE_DIRS} ${LLVM_DEFINITIONS})
    string(REPLACE ";" " " LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
    set(LLVM_LDFLAGS -L${LLVM_LIBRARY_DIRS})
    llvm_map_components_to_libraries(LLVM_LIBS ${PSI_LLVM_MODULES})
  else()
    set(PSI_TVM_LLVM 0)
  endif()
endif()

if(PSI_TVM_LLVM AND NOT LLVM_VERSION VERSION_EQUAL "3.3")
  message(SEND_ERROR "LLVM must be version 3.3, you have ${LLVM_VERSION}")
endif()

if(CMAKE_COMPILER_IS_GNUCC)
  set(PSI_TVM_CC_SYSTEM_KIND_DEFAULT "gcc")
elseif(CMAKE_C_COMPILER MATCHES clang)
  set(PSI_TVM_CC_SYSTEM_KIND_DEFAULT "clang")
endif()

set(PSI_TVM_CC_SYSTEM_PATH ${CMAKE_C_COMPILER} CACHE FILEPATH "Path to system C compiler")
set(PSI_TVM_CC_SYSTEM_KIND ${PSI_TVM_CC_SYSTEM_KIND_DEFAULT} CACHE STRING "Kind of C compiler at PSI_TVM_CC_SYSTEM_PATH, should be 'gcc', 'clang' or 'tcc'")

# Find TCC
find_library(TCC_LIB tcc)
find_path(TCC_INCLUDE libtcc.h)
find_path(TCC_PATH libtcc1.a)
find_path(TCC_CC_INCLUDE stdio.h)
if(TCC_LIB AND TCC_INCLUDE AND TCC_PATH AND TCC_CC_INCLUDE)
  # Whether TCC has been compiled in to the C backend
  set(PSI_TVM_CC_TCCLIB 1)
  set(PSI_TVM_CC_TCC_INCLUDE ${TCC_CC_INCLUDE})
  set(PSI_TVM_CC_TCC_PATH ${TCC_PATH})
else()
  set(PSI_TVM_CC_TCCLIB 0)
endif()

if(WIN32 OR NOT PSI_TVM_LLVM)
  if (PSI_TVM_CC_TCCLIB)
    set(PSI_TVM_JIT_DEFAULT "tcclib")
  else()
    set(PSI_TVM_JIT_DEFAULT "cc")
  endif()
else()
  set(PSI_TVM_JIT_DEFAULT "llvm")
endif()

set(PSI_TVM_JIT "llvm" CACHE STRING "Default JIT compiler to use")
if(NOT PSI_TVM_LLVM)
  message(SEND_ERROR "LLVM not found, please configure LLVM location or select another JIT backend")
endif()

# Set up useful GCC flags
if(CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

  # Try to eliminate LLVM compilation flags which we don't need
  string(REGEX REPLACE "-[WOfg][^ ]*" "" LLVM_CXX_FLAGS "${LLVM_CXX_FLAGS}")
endif()

# Have to ensure _GLIBCXX_DEBUG is consistently defined everywhere otherwise the binary inteface may break
if (LLVM_CXX_FLAGS MATCHES "_GLIBCXX_DEBUG")
  message(SEND_ERROR "Please compile LLVM without _GLIBCXX_DEBUG defined")
endif()

# Locate Boost
find_package(Boost 1.36.0)

# Locate Bison
find_program(BISON_EXECUTABLE NAMES bison PATHS C:/MinGW/msys/1.0/bin)
if(NOT BISON_EXECUTABLE)
  message(SEND_ERROR "bison not found")
endif()
macro(run_bison bison_src)
  string(REGEX REPLACE "\\.ypp$" ".cpp" bison_dest "${bison_src}")
  string(REGEX REPLACE "\\.ypp$" ".output" bison_report "${bison_src}")
  add_custom_command(
    OUTPUT "${bison_dest}" "${bison_report}"
    COMMAND ${BISON_EXECUTABLE} -v --output="${bison_dest}" --report-file="${bison_report}" "${CMAKE_CURRENT_SOURCE_DIR}/${bison_src}"
    MAIN_DEPENDENCY ${bison_src}
  )
  get_filename_component(bison_path "${bison_src}" PATH)
  set_source_files_properties("${bison_dest}" PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/${bison_path}")
endmacro()

add_custom_target(doc)

find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(Doxyfile.in Doxyfile)
  # This glob is a hack to prevent Doxygen rebuilding all the time
  # It should be mostly okay because when any source files are added some CMakeLists.txt must be updated
  file(GLOB_RECURSE DOXYGEN_INPUTS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.[ch]pp)
  add_custom_command(OUTPUT doxygen-tags.tag
    COMMAND mkdir -p doc-sphinx
    COMMAND rm -rf doc-sphinx/doxygen
    COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
    COMMAND mv doc-doxygen-tmp/html doc-sphinx/doxygen
    COMMAND rmdir doc-doxygen-tmp
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile ${DOXYGEN_INPUTS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_target(doc-doxygen DEPENDS doxygen-tags.tag)
  add_dependencies(doc doc-doxygen)
endif()

find_program(RST2HTML NAMES rst2html)
if(RST2HTML)
  add_custom_target(readme ${RST2HTML} ${CMAKE_CURRENT_SOURCE_DIR}/README.rst ${CMAKE_CURRENT_BINARY_DIR}/README.html)
  add_dependencies(doc readme)
endif()

find_program(SPHINX_BUILD NAMES sphinx-build)
if(SPHINX_BUILD)
  configure_file(doc-sphinx/conf.py conf.py)
  add_custom_target(doc-sphinx ${SPHINX_BUILD} -c . ${CMAKE_CURRENT_SOURCE_DIR}/doc-sphinx doc-sphinx)
  add_dependencies(doc doc-sphinx)
  add_custom_target(doc-sphinx-epub ${SPHINX_BUILD} -b epub -c . ${CMAKE_CURRENT_SOURCE_DIR}/doc-sphinx doc-sphinx-epub)
  add_dependencies(doc doc-sphinx-epub)
  
  if(DOXYGEN_FOUND)
    add_dependencies(doc-sphinx doc-doxygen)
  endif()
  
  find_program(TAR NAMES tar)

  add_custom_target(doc-tar ${TAR} -cjf doc.tar.bz2 --exclude=.* doc-sphinx)
  add_dependencies(doc-tar doc-sphinx)
endif()

enable_testing()

macro(shared_library_setup name)
  # Ensure MinGW doesn't add lib* prefix to generated DLLs
  if (WIN32)
    set_target_properties(${name} PROPERTIES PREFIX "")
  endif()
endmacro()

add_subdirectory(src)
add_subdirectory(tests)
