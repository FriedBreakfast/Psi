Project setup
=============

Visual Studio
-------------

Boost should be built using the command line::
  b2 --prefix={PREFIX} --with-test link=shared runtime-link=shared threading=multi install
  
json-c should be downloaded from GitHub.
A couple of files must be patched to work, namely :file:`json_util.c` to remove C99 variable
declarations and :file:`json_inttypes.h` so that :file:`inttypes.h` is assumed to be missing.
:file:`json_config.h` and :file:`config.h` must also be created.

Setting up the CMake build on Windows requires defining several variables,
some of which are not apparent in the CMake GUI.
The list of variables required is

  * :makevar:`BOOST_ROOT`: Set to the install prefix of Boost.
  * :makevar:`BISON_EXECUTABLE`: Set to the path to Bison. Bison is easily available in MinGWs MSYS
  distribution as the package msys-bison. This can be fetched from the MinGW command line using::
  
    mingw-get install msys-bison
  
  The executable will appear at :file:`msys/1.0/bin/bison.exe` relative to the MinGW root.
  * :makevar:`LLVM_CONFIG_INCLUDE`: Path to :file:`LLVMConfig.cmake` which should be installed
  at :file:`share/llvm/LLVMConfig.cmake` relative to the LLVM installation directory when LLVM
  is built via CMake in Visual Studio.
  * :makevar:`JSONC_INCLUDE`: Set to root directory of json-c source.
  * :makevar:`JSONC_LIB`: Should be something like :file:`Release/json.lib` relative to the
  root directory of json-c.