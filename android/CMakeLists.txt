cmake_minimum_required(VERSION 2.6)
project(Psi-Android)

set(ANDROID_PLATFORM android-14)

set(PSI_SOURCE "${CMAKE_SOURCE_DIR}/.." CACHE STRING "Root Psi source directory")
set(PSI_DEBUG FALSE CACHE BOOL "Enable debug assertions")
set(PSI_TESTING FALSE CACHE BOOL "Enable testing libraries")
mark_as_advanced(PSI_SOURCE)
find_path(BOOST_INCLUDE_DIR NAMES boost/optional.hpp DOC "Boost include path" NO_DEFAULT_PATH)
if(NOT EXISTS ${BOOST_INCLUDE_DIR}/boost/optional.hpp)
  message(FATAL_ERROR "Boost not found. It must also be installed in a non-default location to work around CMake ignoring default paths.")
endif()

# Need to normalise the value of PSI_DEBUG for XML properties
if(PSI_TESTING)
  set(PSI_DEBUG_XML true)
else()
  set(PSI_DEBUG_XML false)
endif()

if(PSI_DEBUG)
  set(CHILD_BUILD_TYPE Debug)
else()
  set(CHILD_BUILD_TYPE MinSizeRel)
endif()

find_path(LLVM_SOURCE llvm.spec.in DOC "LLVM source path")
find_program(LLVM_TABLEGEN llvm-tblgen)

find_program(ANT NAMES ant)
find_program(ANDROID_EXE NAMES android)
find_path(ANDROID_NDK ndk-build DOC "Android NDK path")
set(ANDROID_MAKE_STANDALONE ${ANDROID_NDK}/build/tools/make-standalone-toolchain.sh)

macro(ant_setup subdir type extra_flags)
  configure_file(${subdir}/AndroidManifest.xml.in ${subdir}/AndroidManifest.xml)
  configure_file(${subdir}/ant.properties.in ${subdir}/ant.properties)
  configure_file(${subdir}/jni/Android.mk.in ${subdir}/jni/Android.mk)
  execute_process(COMMAND ${ANDROID_EXE} update ${type} -p ${CMAKE_CURRENT_BINARY_DIR}/${subdir} ${extra_flags})
endmacro()

ant_setup(lib lib-project "-t;${ANDROID_PLATFORM}")
ant_setup(test test-project "-m;../lib")

add_custom_target(lib-native)
add_custom_target(lib-debug ${ANT} debug WORKING_DIRECTORY lib DEPENDS lib-native)
add_custom_target(lib-release ${ANT} release WORKING_DIRECTORY lib DEPENDS lib-native)

add_custom_target(test-debug ${ANT} debug WORKING_DIRECTORY test DEPENDS lib-debug)
add_custom_target(test-release ${ANT} release WORKING_DIRECTORY test DEPENDS lib-release)
#execute_process(COMMAND ${ANDROID_NDK}/ndk-build WORKING_DIRECTORY test)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/target)

function(add_platform name arch cmake_arch llvm_arch llvm_triple)
  set(arch_dir ${CMAKE_CURRENT_BINARY_DIR}/target/${name})
  set(toolchain_dir ${arch_dir}/toolchain)
  if(NOT IS_DIRECTORY ${toolchain_dir})
    execute_process(COMMAND ${ANDROID_MAKE_STANDALONE} --install-dir=${toolchain_dir} --platform=${ANDROID_PLATFORM} --ndk-dir=${ANDROID_NDK} --arch=${arch} exit)
    if(exit)
      message(FATAL_ERROR "Toolchain creation for ${name} failed")
    endif()
  endif()
  
  set(common_flags -DANDROID_STANDALONE=${toolchain_dir} -DANDROID_PROCESSOR=${cmake_arch})

  set(toolchain_flags -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/toolchain-android.txt)

  if(NOT IS_DIRECTORY ${arch_dir}/llvm-build)
    file(MAKE_DIRECTORY ${arch_dir}/llvm-build)
    set(llvm_toolchain_flags ${toolchain_flags})
  endif()

  set(psi_backend_flags -DLLVM_DIR=${arch_dir}/llvm-build/share/llvm/cmake)
  execute_process(COMMAND ${CMAKE_COMMAND} ${LLVM_SOURCE} ${llvm_toolchain_flags} ${common_flags}
    -DCMAKE_INSTALL_PREFIX=${arch_dir}/llvm
    -DLLVM_HOST_TRIPLE=${llvm_triple}
    -DLLVM_TABLEGEN=${LLVM_TABLEGEN}
    -DLLVM_TARGETS_TO_BUILD=${llvm_arch}
    
    -DLLVM_BUILD_RUNTIME=0
    -DLLVM_BUILD_TOOLS=0
    
    -DLLVM_ENABLE_ASSERTIONS=0
    -DLLVM_ENABLE_BACKTRACES=0
    -DLLVM_ENABLE_TERMINFO=0
    -DLLVM_ENABLE_TIMESTAMPS=0
    -DLLVM_ENABLE_ZLIB=0

    -DLLVM_INCLUDE_DOCS=0
    -DLLVM_INCLUDE_EXAMPLES=0
    -DLLVM_INCLUDE_TESTS=0
    -DLLVM_INCLUDE_TOOLS=0
  WORKING_DIRECTORY ${arch_dir}/llvm-build RESULT_VARIABLE exit)
  if(exit)
    message(FATAL_ERROR "LLVM project setup for ${name} failed")
  endif()
  
  add_custom_target(${name}-backend ${CMAKE_COMMAND} --build ${arch_dir}/llvm-build --target install)
  if(NOT IS_DIRECTORY ${arch_dir}/psi-build)
    file(MAKE_DIRECTORY ${arch_dir}/psi-build)
    set(psi_toolchain_flags ${toolchain_flags} -G${CMAKE_GENERATOR})
  endif()
  
  execute_process(COMMAND ${CMAKE_COMMAND} ${PSI_SOURCE} ${psi_toolchain_flags} ${psi_backend_flags} ${common_flags}
    -DCMAKE_INSTALL_PREFIX=${arch_dir}/psi
    -DCMAKE_BUILD_TYPE=${CHILD_BUILD_TYPE}
    -DPSI_DEBUG=${PSI_DEBUG}
    -DPSI_COMBINE_LIBRARIES=1
    -DPSI_COMBINE_MODULES=1
    -DBOOST_INCLUDE_DIR=${BOOST_INCLUDE_DIR}
    -DBUILD_TESTING=${PSI_TESTING}
  WORKING_DIRECTORY ${arch_dir}/psi-build RESULT_VARIABLE exit)
  if(exit)
    message(SEND_ERROR "Failed to generate child target ${name}")
  endif()
  
  set(libdir ${CMAKE_CURRENT_BINARY_DIR}/lib/libs/${name})
  add_custom_target(${name}-psi ${CMAKE_COMMAND} --build ${arch_dir}/psi-build --target install/strip
    COMMAND ${CMAKE_COMMAND} -E copy ${arch_dir}/psi/lib/libpsi-combined.so ${libdir}/libpsi-combined.so DEPENDS ${name}-backend)
  add_dependencies(lib-native ${name}-psi)
  if(PSI_TESTING)
    add_custom_target(${name}-psi-test ${CMAKE_COMMAND} --build ${arch_dir}/psi-build --target psi-tvm-test
      COMMAND ${CMAKE_COMMAND} -E copy ${arch_dir}/psi-build/src/libpsi-tvm-test.so ${libdir}/libpsi-tvm-test.so DEPENDS ${name}-backend)
    add_dependencies(lib-native ${name}-psi-test)
  endif()
endfunction()

add_platform(armeabi arm armv7 ARM arm-unknown-linux-androideabi)
#add_platform(x86 x86 X86)
