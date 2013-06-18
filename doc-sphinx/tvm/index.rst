.. _psi.tvm:

TVM
===

.. toctree::
   :maxdepth: 2

   design
   syntax
   instructions
   glossary

TVM is the *typed virtual machine*.
It is a low level language which has a structure similar to :abbr:`SSA (Single static assignment)` form
(see `here <http://en.wikipedia.org/wiki/Static_single_assignment_form>`_).
The instruction set design is similar to `LLVM <http://llvm.org>`_ except for a major difference in
the type system: simple dependent types are supported.
