Storage specifiers
==================

Local variables and parameters have storage specifiers.
These specify whether a variable should be copied or moved in various situations, and whether it is passed by reference or by value.
Aggregate members do not have these specifiers: they must be handled manually, that is, references should be translated to pointers.

Function arguments may have the following storage specifiers, inspired by C++.
However, for us the distinction between l-value and r-value references is simply the behaviour of object moving and copying choices.
There is no notion of const vs. non-const references.
Function argument storage is described by the :psi:`ParameterMode<Psi::Compiler::ParameterMode>` enumeration.

Input
  Used when a function with either not modify an object or will modify it in a way useful to the caller.

Output
  A pure output parameter. It is an error if this is a temporary since the function call is not useful unless this result is captured.

Input/Output
  A parameter used for both input and output.
  This is only distinguished from an input parameter by the fact that functional values will not be up-converted to input/output parameters.

R-value
  Used when a function will modify the object destructively so that the caller should use a copy if it would like to use the object again.
  This is used (as in C++ r-value references) to implement efficient transfer of resources between objects without needless copying.

Functional
  Used for passing dependent type information.
  This currently must be passed in virtual registers since it must be visible to TVMs type system.
  Only simple types may be passed this way because object copying and moving semantics cannot be implemented.

Function return values can have the following specifiers, described by the :psi:`ResultMode<Psi::Compiler::ResultMode>` enumeration:

By value
  The value is returned to the caller in the caller's stack space.
L-value reference
  A reference to an object.
  The caller may explicitly modify the object but should not modify it implicitly by passing it as an R-value reference to another function.
R-value reference
  A reference to an object.
  The caller may implicitly and explicitly modify the object.

Local storage breaks down into the following categories:

Functional
  An object which is constant and lives in registers. It must have a simple type.
Stack
  A normal object, allocated on the stack.
L-value reference
  A pointer to an L-value.
R-value reference
  A pointer to an R-value.

The behaviour of these storage types depends on whether the variable in question is about to go out of scope.
In either case, the behaviour is categorised as "Functional", "L-value" or "R-value", as follows:


.. _psi.internals.storage_specifiers.scope-exit-var:
.. table:: Variable behaviour including scope exit case.
  
  ================= ================ =============
  Storage           Normal behaviour At scope exit
  ================= ================ =============
  Functional        Functional       Functional
  Stack             L-value          R-value
  L-value reference L-value          L-value
  R-value reference L-value          R-value
  ================= ================ =============

Local and parameter storage specifiers lead to the following parameter passing behaviour (note that this is the true definition of the different storage types):

.. list-table:: Parameter passing mechanism by storage type
  :header-rows: 1
  :stub-columns: 1
  
  * -
    - Input
    - Output
    - Input/Output
    - R-value
    - Functional
  * - L-value
    - Reference
    - Reference
    - Reference
    - Copy
    - Error
  * - R-value
    - Reference
    - Error
    - Reference
    - Reference
    - Error
  * - Functional
    - Bit-wise copy
    - Error
    - Error
    - Bit-wise copy
    - By value
  
Some explanations:

* It is an error to use a functional parameter for output since they are part of the type system and cannot be modified.
* It is an error to use an r-value for output since it is expected that an r-value will not accessible after the call, and hence the output data cannot be accessed.
* Copy for a functional value is always equivalent to bit wise copy since the type must be simple.

For return and jump argument values, values in the local stack frame are either moved or copied depending
on :ref:`this table<psi.internals.storage_specifiers.scope-exit-var>`, however they cannot be changed to references since
the storage is about to be destroyed.
Return values are treated as a special case of jump arguments where the destination scope is contains no
local variables.


.. list-table:: Parameter return and jump argument passing mechanism by storage type
  :header-rows: 1
  :stub-columns: 1
  
  * -
    - By value
    - L-value reference
    - R-value reference
  * - Functional
    - Bit-wise copy
    - Error
    - Error
  * - Stack
    - Move/Copy
    - Error
    - Error
  * - L-value reference
    - Copy
    - Reference
    - Error
  * - R-value reference
    - Move
    - Reference
    - Reference
