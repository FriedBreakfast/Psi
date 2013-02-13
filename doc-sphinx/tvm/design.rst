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
    call %f (array i8 (add n #i1)) %x1 %x2;
    return #i0;
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

  %vtable = recursive (%tag : upref_type) > (struct
    (pointer (function (pointer (apply %base %tag) %tag) > i32))
  );
  
  %base = recursive (%tag : upref_type) > (struct
    (pointer (apply %vtable %tag))
  );
  
  %func = function (%obj_wrapped : exists (%tag : upref_type) > (pointer (apply %base %tag) %tag)) > i32 {
    %obj = unwrap %obj_wrapped;
    %vptr = load (gep %obj #up0);
    %callback = load (gep %vptr #up0);
    %val = call %callback %obj;
    return %val;
  };
  
This second example shows how to implement full single inheritance, including extending the type and vtable at the same time::

  %vtable = recursive (%vtag : upref_type, %tag : upref_type) > (struct
    (pointer (function (pointer (apply %base %vtag %tag) %tag) > i32))
  );
  
  %vtable_derived = recursive (%vtag : upref_type, %tag : upref_type) > (struct
    (apply %vtable (upref (apply %vtable_derived %vtag %tag) #up0 %vtag) %tag)
    (pointer (function (pointer (apply %derived %vtag %tag) %tag) > i32))
  );
  
  %base = recursive (%vtag : upref_type, %tag : upref_type) > (struct
    (pointer (apply %vtable %vtag %tag) %vtag)
    i32
  );
  
  %derived = recursive (%vtag : upref_type, %tag : upref_type) > (struct
    (apply %base (upref (apply %vtable_derived %vtag %tag) #up0 %vtag) (upref (apply %derived %vtag %tag) #up0 %tag))
    i32
  );
  
  %func = function (%obj_wrapped : exists (%vtag : upref_type, %tag : upref_type) > (pointer (apply %derived %vtag %tag) %tag)) > i32 {
    %obj = unwrap %obj_wrapped;
    %vptr_base = load (gep (gep %obj #up0) #up0);
    %vptr = outer_ptr %vptr_base;
    %callback1 = load (gep %vptr #up1);
    %callback2 = load (gep (gep %vptr #up0) #up0);
    %val1 = call %callback1 %obj;
    %val2 = call %callback2 %obj;
    return (add %val1 %val2);
  };

See:

* :ref:`psi.tvm.instructions.outer_ptr`
* :ref:`psi.tvm.instructions.exists`
* :ref:`psi.tvm.instructions.unwrap`

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

Aggregate type lowering
-----------------------

Type lowering generally tries to rewrite TVM code into TVM code which
contains simpler operations but only making the smallest possible modification.
Therefore if the back-end supports structures and fixed-size arrays
(and, indeed, unions) these will be passed through unmodified.
In cases where a data structure is not understood by the back-end data structures
with a fixed number of elements are split into their constituent parts and separate
operations are generated to handle each part.
In cases where the numer of parts is not fixed (i.e. a data "blob" or a variable length array)
the data is stored on the stack.
Note that it is not possible for an ``array_el`` operation to get a pointer into such a variable
length array because this makes phi operations impossible to track; no interior pointers are allowed.

Unions require special handling within this scheme because they are generally treated as "blobs"
however constructing a union requires creating a value with a single element.
Care must be taken to ensure this case is handled correctly because the element will be padded,
and the ``LoweredValue`` associated to a union will be split even though the 
associated ``LoweredType`` indicates a blob.

Back-end instructions
"""""""""""""""""""""

Certain instructions are present whose use is discouraged.
These exist to facilitate a TVM compilation stage where dependent
types are rewritten as fixed types.
When doing this, operations which previously acted on
registers may now act on the stack and thus require memory-to-memory
rather than register-to-memory operations,
and it is easier to have instructions and types present to handle
these cases than to call external functions.

Notes on ``exists``
-------------------

``exists {a} > pointer {b}`` is not equivalent to ``pointer (exists {a} > {b})``.
To see this, consider the implication of making this relation transitive, then::

  pointer (pointer (exists {a} > {b}))

would be equivalent to::

  exists {a} > pointer (pointer {b})

which cannot possibly be the case since in the first case the inner pointer can
clearly be assigned to any memory location matching the pattern and in the
second case this is not allowed because it is the pointer itself whose type is
not fully known.