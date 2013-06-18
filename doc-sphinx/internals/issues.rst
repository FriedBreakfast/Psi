Issues
======

Symbol naming
-------------

The symbol naming mechanism works, but is currently fragile.

Symbols basically break into two categories: global and non-global.
Global symbols must have a predictable name since they may be linked between modules.
Non-global symbols can have their names modified to make them unique.

Global symbols fall into several categories:

* Functions. In particular this requires naming interface implementation members
  systematically.
* Function specializations. Optimizations will eventually generate functions with certain
  parameters fixed; systematically naming these symbols will help merge them during linking.
* Interface implementation instantiations may be global; it would be convenient for debugging
  purposes if a naming scheme were developed which allowed the interface parameters to be
  inferred.
  
Non-global symbols include local data, closures (not yet implemented).
The current naming scheme is more or less acceptable here.

I therefore need to create a new :psi:`Psi::LogicalSourceLocation` class.
I don't think I need referential uniqueness here since I can enforce uniqueness during TVM
module generation; however each instance will carry a flag indicating whether the name is global or not
(note that a global node cannot be the child of a non-global node).


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

Compiling and linking
---------------------

* Outputs of compilation are objects and modules. Modules are executables or shared objects;
  objects are like ``.o`` files.

* An object may be composed of several source files; a module will usually comprise several objects.

* The compiler must know during compilation which module will eventually contain any given symbol so
  that it can automatically handle import/export linkage.