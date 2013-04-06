Issues
======

Constant folding
----------------

I should put in a mechanism to enable functional terms to be optimized where possible.

Signed integers
---------------

The present handling of signed integers and integer constant folding is rather dubious.
For example, the C backend generates large positive offsets and relies on wraparound behaviour
rather than using a small negative offset when the ``outer_ptr`` instruction is used.
It is not immediately obvious what to do: I created the current system to avoid a proliferation
of similar instructions (i.e. only add/neg rather than add/sub/neg) but this seems to
make it difficult to do unsigned subtraction without relying on integer wraparound, which
of course is representation dependent and therefore suggests I should have simply not
supported signed integers in the first place.

The bottom type
---------------

At some point I need to define an interface which details how to merge differing return values of
two trees to create a user-friendly if-then-else statement.
In case one or both of those types is the bottom type, the interface will expect a function that
takes a value of ``BottomType`` and return some other type.
Obviously no such function can be written, but this can be circumvented by a rule:
any function expecting a parameter of type ``BottomType`` should not have a body,
since it can never be called because the parameter cannot itself be constructed.

It is forbidden to create the tree ``DefaultValue(BottomType())``.
The high level tree representation does not currently have an undefined value tree, and I do not
see a reason this will change, but if it does, then an undefined value of bottom type should also
be forbidden.

Closures
--------

Need to scan for variables used so I can generate the internal closure variable type.
Closures simply don't exist at the moment.
I also need to allow local interface implementations, which will interact with closures.

Syntax
------

Constructor expressions
"""""""""""""""""""""""

When assigning to something whose type is known, use the known type information to
allow the user to just write the necessary values without having to give the type
information again.

Could be useful for struct, union or function pointer types.

Specialization and parameterization
-----------------------------------

I'm not at all sure that the current implementation works properly.
It's almost certainly buggy because of the complexity.
I think this whole system needs a rethink.

TVM improvements
----------------

* Need to ensure any missing ``freea`` instructions are inserted during
  aggregate lowering so that further backends need not worry about whether
  each ``alloca`` instruction has an associated ``freea``.

* Constant folding should not be performed by the FunctionalBuilder class.
  Instead it should be performed by each class itself, which will mean:
  
  * Type checking can be run before constant folding in debug mode without
    causing problems in the code (this choice will be made in only one place),
    so no slip ups in term generation will get through this way.
    
  * It is not possible to construct a term in non-canoncal form, although
    I believe I'm using the FunctionalBuilder class everywhere at the moment
    so this should not be happening.
  
* Several cases of constant folding are missing, particularly bit-wise operations.

* Function parameter attributes, particularly ``nonnull``, which should be set for
  reference-type arguments.
  
* Add some sort of hardware exception mechanism: currently division-by-zero and NULL
  pointer errors will cause program termination.
  It would be better to provide some options to the user to let this be handled gracefully
  most of the time, or to remove error checking when performance is needed (locally, not
  globally though).

C backend
"""""""""

* Handle ``load`` instructions better: currently they are always assigned to local variables
  if the result is used. However if the result is only used once and a ``store`` instruction
  does not occur between the ``load`` instruction an this use, no local variable is required.

* libtcc implementation.
