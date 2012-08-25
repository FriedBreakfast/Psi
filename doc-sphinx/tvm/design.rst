Design
======

Dependent types
---------------

The presence of dependent types requires a sligtly different approach to expressions than
a normal procedural language, since the notion of value equivalence must be built in.
To give an example::

  %f = function(%t: type, %a: pointer %t, %b: pointer %t) > pointer %t;

  %g = function (%n: i32) > i32 {
    %x1 = alloca (array i8 (add %n #i1));
    %x2 = alloca (array i8 (add %n #i1));
    %g(array i8 (add n #i1), %x1, %x2);
  };

In order for the function ``g`` to pass a type check, the checker must be able to detect
that ``x1`` and ``x2`` have the same type, despite the fact that this type is dependent
on a number computed during the function.
If the expression ``add %n #i1`` is treated as a machine instruction which is executed
at a definite time, it takes considerable effort to identify two different expressions
as having the same value. Instead TVM divides expressions into two types: functional operations
and instructions.

Functional operations depend solely on their arguments and each evaluation of the same
expression has the same value.
``add`` is an example of a numeric functional operation.
All types are also functional operations, for example ``array``.
Instructions execute at a definite time, depend on machine state and may return a
different value if executed with the same parameters more than once.
``alloca`` is an example.

.. _psi.tvm.phantom_values:

Phantom values
--------------

.. _psi.tvm.virtual_functions:

Virtual functions
-----------------

Virtual function calls are performed using a combination of member pointers and base pointers.
For a single dispatch scenario, the following code can be used::

  %vtable = recursive (%derived : type, %offset : member %derived (apply %base %derived)) > struct 
    (pointer (function (member_ptr %offset) > i8))
  ;
  
  %base = base %derived %offset > struct
    (pointer (apply %vtable %derived %offset))
  ;
  
  %func = function (%obj : base_ptr %base) > i8 {
    %derived = unbase %base;
    %vptr = load (struct_ep (member_inner %derived_ptr) #i0);
    %callback = load (struct_ep %vptr #i0);
    %val = call %callback %derived;
  };

See:

* :ref:`psi.tvm.instructions.member_ptr`
* :ref:`psi.tvm.instructions.base_ptr`

Exceptions
----------

When an exception is raised, the exception handling code must be able to map the current
instruction pointer in a function to an appropriate block in the same function containing
cleanup and/or exception handling code.
Such blocks are called landing pads, and landing pads for each location are specified on
a per-block level.
This means that to change landing pads a new block must be created and jumped to, even
if the jump is unconditional.

Whilst the additional blocks are something of a downside, the upside is that in principle
all operations may raise an exception, not just those specifically designed to do so.
This could allow arithmetic overflows and NULL pointer errors to be handled by the
exception handling mechanism, however the current implementation is based on LLVM so
in fact only function calls may raise exceptions.

.. _psi.tvm.type_lowering:

Type lowering
-------------

Certain instructions are present whose use is discouraged.
These exist to facilitate a TVM compilation stage where dependent
types are rewritten as fixed types.
When doing this, operations which previously acted on
registers may now act on the stack and thus require memory-to-memory
rather than register-to-memory operations,
and it is easier to have instructions and types present to handle
these cases than to call external functions.
