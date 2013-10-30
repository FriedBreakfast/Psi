cmake_minimum_required(VERSION 2.6)
project(Psi-Android)

set(ANDROID_PLATFORM android-14)

set(PSI_SOURCE "${CMAKE_SOURCE_DIR}/.." CACHE STRING "Root Psi source directory")
set(PSI_DEBUG FALSE CACHE BOOL "Enable debug assertions")
set(PSI_TESTING FALSE CACHE BOOL "Enable testing libraries")
set(LLVM_BUILD_JOBS 6 CACHE STRING "Number of parallel jobs to run building LLVM")
mark_as_advanced(PSI_SOURCE)
find_path(BOOST_INCLUDE_DIR NAMES boost/optional.hpp DOC "Boost include path" NO_DEFAULT_PATH)
if(NOT EXISTS ${BOOST_INCLUDE_DIR}/boost/optional.hpp)
  message(FATAL_ERROR "Boost not found. It must also be installed in a non-default location to work around CMake ignoring default paths.")
endif()

# Need to normalise the value of PSI_DEBUG for XML properties
if(PSI_DEBUG)
  set(PSI_DEBUG_XML true)
else()
  set(PSI_DEBUG_XML false)
endif()

find_path(LLVM_SOURCE llvm.spec.in DOC "LLVM source path")
find_program(LLVM_TABLEGEN llvm-tblgen)

find_program(ANT NAMES ant)
find_program(ANDROID_EXE NAMES android)
find_path(ANDROID_NDK ndk-build DOC "Android NDK path")
set(ANDROID_MAKE_STANDALONE ${ANDROID_NDK}/build/tools/make-standalone-toolchain.sh)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/target)
function(add_platform name arch cmake_arch llvm_arch llvm_triple)
  set(arch_dir ${CMAKE_CURRENT_BINARY_DIR}/target/${name})
  set(toolchain_dir ${arch_dir}/toolchain)
  if(NOT IS_DIRECTORY ${toolchain_dir})
    execute_process(COMMAND ${ANDROID_MAKE_STANDALONE} --install-dir=${toolchain_dir} --platform=${ANDROID_PLATFORM} --ndk-dir=${ANDROID_NDK} exit)
    if(exit)
      message(FATAL_ERROR "Toolchain creation for ${name} failed")
    endif()
  endif()

  set(toolchain_flags
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/toolchain-android.txt
    -DANDROID_STANDALONE=${toolchain_dir}
    -DANDROID_PROCESSOR=${cmake_arch}
  )
  set(common_flags -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})

  if(NOT IS_DIRECTORY ${arch_dir}/llvm-build)
    file(MAKE_DIRECTORY ${arch_dir}/llvm-build)
    set(llvm_toolchain_flags ${toolchain_flags} -DLLVM_HOST_TRIPLE=${llvm_triple})
  endif()
    
  execute_process(COMMAND ${CMAKE_COMMAND} ${LLVM_SOURCE} ${common_flags} ${llvm_toolchain_flags}
    -DLLVM_TABLEGEN=${LLVM_TABLEGEN}
    -DLLVM_BUILD_TOOLS=0
    -DLLVM_TARGETS_TO_BUILD=${llvm_arch}
    -DCMAKE_INSTALL_PREFIX=${arch_dir}/llvm
  WORKING_DIRECTORY ${arch_dir}/llvm-build RESULT_VARIABLE exit)
  if(exit)
    message(FATAL_ERROR "LLVM project setup for ${name} failed")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} --build ${arch_dir}/llvm-build --target install -- -j ${LLVM_BUILD_JOBS} RESULT_VARIABLE exit)
  if(exit)
    message(FATAL_ERROR "Failed to compile and install LLVM")
  endif()
  
  if(NOT IS_DIRECTORY ${arch_dir}/psi)
    file(MAKE_DIRECTORY ${arch_dir}/psi)
    set(psi_toolchain_flags ${toolchain_flags} -G${CMAKE_GENERATOR})
  endif()

  execute_process(COMMAND ${CMAKE_COMMAND} ${PSI_SOURCE} ${common_flags} ${psi_toolchain_flags}
    -DLLVM_DIR=${arch_dir}/llvm/share/llvm/cmake
    -DPSI_DEBUG=${PSI_DEBUG}
    -DPSI_COMBINE_LIBRARIES=1
    -DPSI_COMBINE_MODULES=1
    -DBOOST_INCLUDE_DIR=${BOOST_INCLUDE_DIR}
    -DBUILD_TESTING=${PSI_TESTING}
  WORKING_DIRECTORY ${arch_dir}/psi RESULT_VARIABLE exit)
  if(exit)
    message(SEND_ERROR "Failed to generate child target ${name}")
  endif()
  
  list(APPEND ndk_targets ${name})
  set(ndk_targets ${ndk_targets} PARENT_SCOPE)
endfunction()

add_platform(armeabi arm armv7 ARM arm-unknown-linux-androideabi)
#add_platform(x86 x86 X86)

function(add_library child name)
  foreach(arch IN LISTS ndk_targets)
    set(child_target ${child}-${name}-${arch})
    set(child_path ${CMAKE_CURRENT_BINARY_DIR}/target/${arch}/psi/src/lib${name}.so)
    set(my_path ${CMAKE_CURRENT_BINARY_DIR}/${child}/libs/${arch}/lib${name}.so)
    add_custom_target(${child_target}
    ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/target/${arch}/psi --target ${name}
    COMMAND ${CMAKE_COMMAND} -E copy ${child_path} ${my_path})
    list(APPEND libfiles ${child_target})
  endforeach()
  set(${child}_libs ${${child}_libs} ${libfiles} PARENT_SCOPE)
endfunction()

macro(ant_setup subdir type extra_flags)
  configure_file(${subdir}/AndroidManifest.xml.in ${subdir}/AndroidManifest.xml)
  configure_file(${subdir}/ant.properties.in ${subdir}/ant.properties)
  configure_file(${subdir}/jni/Android.mk.in ${subdir}/jni/Android.mk)
  execute_process(COMMAND ${ANDROID_EXE} update ${type} -p ${CMAKE_CURRENT_BINARY_DIR}/${subdir} ${extra_flags})
endmacro()

ant_setup(lib lib-project "-t;${ANDROID_PLATFORM}")

add_library(lib psi-combined)
if(PSI_TESTING)
  # This should go in the test project, but Android doesn't appear to look there fore native binaries
  add_library(lib psi-tvm-test)
endif()
add_custom_target(lib-debug ${ANT} debug WORKING_DIRECTORY lib DEPENDS ${lib_libs})
add_custom_target(lib-release ${ANT} release WORKING_DIRECTORY lib DEPENDS ${lib_libs})

ant_setup(test test-project "-m;../lib")

add_custom_target(test-debug ${ANT} debug WORKING_DIRECTORY test DEPENDS lib-debug ${test_libs})
add_custom_target(test-release ${ANT} release WORKING_DIRECTORY test DEPENDS lib-release ${test_libs})
execute_process(COMMAND ${ANDROID_NDK}/ndk-build WORKING_DIRECTORY test)
