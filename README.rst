.. |llvm version| replace:: 3.3

Psi
===

Psi is planned to be a programming language in the spirit of C++.
It's very much a work in progress right now, and won't be in a
usable state of any description for some time.
It's entirely possible that the Git master branch will be broken at any time.

.. contents::

Features
--------

That means a statically typed language which is good at systems programming and should interact
with existing C libraries easily.
It also means a good macro system so that boilerplate code can be avoided.
At the moment the most complete part of the project is TVM, a low level quasi-assembly language with a
more sophisticated type system, designed to handle ABI level platform dependencies.


A summary of the planned features:

* Generic/template types. Generics are statically type checked once and allow packing of
  parameter types inline rather than having to store them by reference, combining the benefits
  of both the C++ and Java approach.
* Macros can be written in Psi itself, and code is generated in a multi-stage compile process.
* C++ style deterministic lifecycles for local objects.
* No garbage collection, for now. As a systems programming language I don't want to force a runtime
  environment on users.

Building
--------

I'm going to assume you're familiar with building programs, specifically with CMake_
since that's the build system I've used.
The following targets exist:

* Linux x86
* Linux x86-64
* Windows x86, although support is preliminary and not all tests work yet.

You will need the following tools:

* CMake_
* C++ compiler: you must use `GNU G++`_ on Linux. On Windows either MinGW_ or `Visual C++`_
  can be used.
* LLVM_: must be version |llvm version| in order to work correctly. Earlier and later
  versions will probably not work since the LLVM project tends to tweak their API a little
  in each version. It's usually best to download and build LLVM manually rather than attempt
  to get it from a Linux package manager.
* Boost_: only header files are used; not compiled libraries so just unpacking the
  Boost distribution is enough.
* `GNU Bison`_: Required to build the parser.
  It is available for Windows from `GNU Bison for Windows`_, or can be downloaded as
  an MSYS_ package.

On Ubuntu the required dependencies except for LLVM can be downloaded with::

  sudo apt-get install g++ cmake bison libboost-dev

The important CMake variables for configuring the project are:

* ``LLVM_CONFIG`` and ``LLVM_DIR``: ``LLVM_CONFIG`` is the location of the ``llvm-config``
  program if LLVM was not built using CMake. If LLVM was built using CMake, ``LLVM_DIR``
  is the directory containing ``LLVMConfig.cmake``.
  
* ``Boost_INCLUDE_DIR``: location of Boost headers.

* ``BISON_EXECUTABLE``: Location of GNU Bison.

* ``PSI_DEBUG``: enable debug assertions.

.. _CMake: http://cmake.org/
.. _LLVM: http://llvm.org/
.. _Boost: http://www.boost.org/
.. _GNU Bison: http://www.gnu.org/software/bison/
.. _GNU Bison for Windows: http://gnuwin32.sourceforge.net/packages/bison.htm
.. _GNU G++: http://gcc.gnu.org/
.. _MinGW: http://www.mingw.org/
.. _MSYS: http://www.mingw.org/wiki/MSYS
.. _Visual C++: http://msdn.microsoft.com/visualc/
.. _Ninja: http://martine.github.io/ninja/

Non-essential dependencies
""""""""""""""""""""""""""

Several pieces of software are useful but not required for building Psi.

* Python_: Test wrapper scripts are written in Python, so most tests are
  dependent on a Python install being available.
* Sphinx_: Required to build HTML and ePub documentation.
* Doxygen_: Generate C++ class documentation. This is only useful if you're planning
  to work on Psi.
* Docutils_: Used to generate the HTML version of this readme file. It should be installed
  as part of Sphinx if you have that.

.. _Python: http://python.org/
.. _Sphinx: http://sphinx-doc.org/
.. _Doxygen: http://www.doxygen.org/
.. _Docutils: http://docutils.sourceforge.net/
