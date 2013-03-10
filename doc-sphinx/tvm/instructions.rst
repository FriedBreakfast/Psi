Instructions
============

Note that the notation ``{name}`` denotes an expression, ``{`` and ``}`` are not part of the syntax.

Types
-----

Primitive types, and operations for constructing aggregate types.

array
"""""

``array {t} {n}``

Array type.

``{t}``
  Type of each element in the array.
``{n}``
  Length.

bool
""""

``bool``

Boolean type.

byte
""""

``byte``

The smallest addressable unit in memory.
This is distinct from integer types because this type is simply used
for pointer arithmetic on unknown types; the contents of a ``byte``
variable may be loaded, stored and copied, but are not themselves
the input of any operation.

.. note:: Use of this in TVM input is discouraged, unless you
  are interacting with C functions or some other case where
  a type representing an unknown block of data is desired.
  It primarily exists to facilitate :ref:`type lowering <psi.tvm.type_lowering>`.

.. _psi.tvm.instructions.const:

const
"""""

``const {value}``

A type which can only take a single value given by ``value``.
This is used to allow phantom values to be stored to an retrieved from memory, using the ``solidify`` instruction.

``value``
  The only valid value of this type.

empty
"""""

``empty``

The empty type. This type holds no data.
It is regularly used in place of ``void`` in C since every expression in TVM has a type.

i\ *n*
""""""

``i8``
``i16``
``i32``
``i64``
``iptr``

Signed integer types of various bit widths.
``iptr`` has a platform dependent bit width, equal to the width of a pointer.
Since type-checking is performed in a platform independent way, this type is
not equivalent to any of the other types.

.. _psi.tvm.instructions.pointer:

pointer
"""""""

``pointer {type} [{upwards}]``

A reference to a value in memory of type ``{type}``.

``{type}``
  Pointed-to type.
``{upwards}``
  A trail of how this pointer was obtained, which allows reverse member lookup to be performed.
  This should have type :ref:`psi.tvm.instructions.upref`.

.. _psi.tvm.instructions.struct:

struct
""""""

``struct {members...}``

A data type containing a sequence of elements of different types.

``{members...}``
  A list of types which are members of this struct type.

type
""""

``type``

The fundamental type. The type of all other expressions in this section
is ``type``. ``type`` itself has no type.

ui\ *n*
"""""""

``ui8``
``ui16``
``ui32``
``ui64``
``uiptr``

Unsigned integer types of various bit widths.
``uiptr`` has a platform dependent bit width, equal to the width of a pointer.
Since type-checking is performed in a platform independent way, this type is
not equivalent to any of the other types.

.. _psi.tvm.instructions.union:

union
"""""

``union {members...}``

A data type holding a value of one of its members.

``{members...}``
  A list of types which are members of this union type.
  
Note that type equivalence for union types is based on *exact* equivalence of
the ``{members...}`` list; two lists which produce the same memory layout but are
different (e.g. by having an element repeated) are considered different.

Note that the old C-style trick for getting parts of integers::

  %s = array i8 #up4
  %t = union i32 %t
  %a = union_v %t #i432098
  %b = array_el (union_el %t %s) #up1

Is valid even at the virtual register level.
Obviously the results are not portable though.


Higher types
------------

.. _psi.tvm.instructions.apply:

apply
"""""

``apply {recursive} {parameters...}``

Specialize a recursive type.

``{recursive}``
  A :ref:`psi.tvm.instructions.recursive` term.
``{parameters...}``
  A list of parameters to specialize the generic type with.
  
.. _psi.tvm.instructions.exists:

exists
""""""

``exists ({parameters...}) > {result}``

Turn an expression with a specific type into a generic one.
The parameter list may be empty, although this would be unusual since then the ``exists`` term would serve no purpose.

specialize
""""""""""

``specialize {f} {parameters...}``

Eliminate phantom parameters from a function pointer, thus reducing the general function to a more specific case.

``{f}``
  Function pointer.
``{parameters..}``
  Parameter list. This list must be shorter than the number of phantom parameters to the function.

.. _psi.tvm.instructions.recursive:

recursive
"""""""""

``recursive [({parameters})] > [{type} >] {result}``

Construct a recursive type.
This is not an instruction but a global declaration, and is the only way a type can contain references to itself (i.e. self pointers), since this is how a type is named.

``{type}``
  The type of the expression ``{result}``.
  If not given, this defaults to ``type``, since usually a recursive type rather than a recursive value is desired.

.. _psi.tvm.instructions.unwrap:

unwrap
""""""

``unwrap {e}``

Take an expression whose type is a :ref:`psi.tvm.instructions.exists` value and extract the target value.

.. _psi.tvm.instructions.unwrap_param:

unwrap_param
""""""""""""

``unwrap_param {e} {n}``

The parameter implicitly applied by :ref:`psi.tvm.instructions.unwrap` to create
the result value.

.. _psi.tvm.instructions.upref_type:

upref_type
""""""""""

``upref_type``

Type of upward references.
This is the type of the second argument to :ref:`psi.tvm.instructions.pointer`.


Aggregate operations
--------------------

Operations for constructing and manipulating aggregate types in virtual registers,
and manipulating pointers to aggregate types.

.. _psi.tvm.instructions.apply_v:

apply_v
"""""""

``apply_v {ty} {val}``

Create a value for an :ref:`psi.tvm.instructions.apply` type.

``{ty}``
  Type generated by :ref:`psi.tvm.instructions.apply`.
``{val}``
  A value, which must be the value implied by ``{ty}`` and its associated recursive type.

array_v
"""""""

``array_v {ty} {elements...}``

Array value constructor.

``{ty}``
  Array element type.
  This argument is required so that the array type is known when it has no elements.
``{elements...}``
  List of values of type ``{ty}``.
  The array length is inferred from the length of this list.

element
"""""""

``element {agg} {idx}``

Get the value of an aggregate member.

``{agg}``
  An aggregate value.
``{idx}``
  Member index.

empty_v
"""""""

``empty_v``

Value of the empty type.

gep
"""

``gep {ptr} {idx}``

Get a pointer to an element of an aggregate from a pointer to the aggregate.

``{ptr}``
  Pointer to an aggregate.
``{idx}``
  Index of member to get pointer to. Must be a ``uiptr``.

.. _psi.tvm.instructions.outer_ptr:

outer_ptr
"""""""""

``outer_ptr {ptr}``

Get a pointer to the data structure containing ``{ptr}``.
In order to work, the type of ``{ptr}`` must be sufficiently visible that the second argument to :ref:`psi.tvm.instructions.pointer` is known.

``{ptr}``
  Pointer to interior of a data structure.

pointer_cast
""""""""""""

``pointer_cast {ptr} {type} [{upwards}]``

Cast a pointer to a different pointer type.
The result type of this instruction is ``pointer {type}``.

``{ptr}``
  Pointer to be cast.
  The result of this operation points to the same address as ``{ptr}``.
``{type}``
  Type to cast the pointer to.
  Note that this is not a pointer type itself unless the result is a pointer to a pointer.
``{upwards}``
  Upref list for the new pointer.

pointer_offset
""""""""""""""

``pointer_offset {ptr} {n}``

Add an offset to a pointer.

``{ptr}``
  Base pointer.
``{n}``
  Number of elements to offset this pointer by.
  This should have type ``iptr``.
  Note that this is measure in units of the pointed-to type, not bytes.

.. _psi.tvm.instructions.struct_v:

struct_v
""""""""

``struct_v {elements...}``

Structure value constructor.
Note that an empty structure is not equivalent to ``empty_v``.

``elements``
  List of elements of the structure.
  The structures type is inferred from the type of the elements.

undef
"""""

``undef {type}``

Undefined value of any type.
The compiler is allowed to make any assumption whatsoever about the contents of such a value.
Note that values ``const`` type which are undefined still contain arbitrary data.

``{type}``
  Result type of this operation.
  
.. _psi.tvm.instructions.union_v:

union_v
"""""""

``union_v {type} {value}``

Create a value of union type.

``{type}``
  A union type.
``{value}``
  A value whose type must be one of the members of the union type ``{type}``.
  
.. _psi.tvm.instructions.upref:

upref
"""""

``upref {ty} {idx} {next}``

Structure upward reference.

``{ty}``
  An aggregate type.
``{idx}``
  Index into ``{ty}``.
``{next}``
  Next upward reference.

zero
""""

``zero {type}``

Zero-initialized value of any type.
Note that in one case this does not actually contain the value zero: that of ``const`` types,
which only have a single valid value which is used instead of zeor.

``{type}``
  Result type of this operation.


Arithmetic
----------

Global constants and numerical expressions.
Note that numerical constants are covered in :ref:`psi.tvm.numerical_constants`

add
"""

div
"""

false
"""""

``false``

Boolean false value.

mul
"""

neg
"""

sub
"""

``sub {a} {b}``

This is not an actual TVM instruction, but convenient shorthand for
``add {a} (neg {b})``.

.. note:: The purpose of requiring a pair of operations to perform
  subtraction is to simplify constructing and maintaining additive
  arithmetic operations in a normal form.

true
""""

``true``

Boolean true value.

Instructions
------------

These operations must occur in a definite sequence since they may read or modify memory.

.. _psi.tvm.instructions.alloca:

alloca
""""""

``alloca {type} [{count} [{alignment}]]``

Allocate storage for a type on the stack.

``{type}``
  Type to allocate space for.
  The result type of this instruction is ``pointer {type}``.
``{count}``
  Optionally, the number of elements to allocate. Defaults to 1.
``{alignment}``
  Optional alignment specification. This will be ignored if it is smaller
  then the minimum alignment for ``{type}``. Defaults to 0.
  
.. _psi.tvm.instructions.alloca_const:

alloca_const
""""""""""""

``alloca_const {value}``

Allocate storage on the stack and assign it a constant value.
The contents of this pointer may not be subsequently modified.

``{value}``
  Value to be placed on the stack.

br
""

``br {block}``

Jump to the specified block.

Note that block names are literals, the destination block cannot be
an expression selecting a block because in that case the flow of
control cannot be tracked. To conditionally select a branch target,
see :ref:`psi.tvm.instructions.cond_br`

``{block}``
  Name of a block in this function.
  
call
""""

``call {target} {args...}``

Invoke a function.

``{target}``
  Address of function to call.
  This must have pointer-to-function type.
``{args...}``
  A list of arguments to pass to the function.

.. _psi.tvm.instructions.cond_br:

cond_br
"""""""

``cond_br {cond} {iftrue} {iffalse}``
  
Continue execution at a location dependent on a boolean value.

``{cond}``
  ``bool`` value used to select the jump target.
``{iftrue}``
  Block to jump to if ``{cond}`` is true.
``{iffalse}``
  Block to jump to if ``{cond}`` is false.
  
eval
""""

``eval {value}``

Evaluate an expression.
The expression should be functional but can still fail through CPU traps (such as divide-by-zero).
This instruction ensures that any such failure occurs at a well defined point in the code.
Later use of the same expression (or any sub-expression) is safe, since the value generated here will be re-used.

``{value}``
  A calculation which may trap.
  
freea
"""""

``freea {ptr}``

Restore the stack pointer to its position before ``{ptr}`` was allocated.

``{ptr}``
  Point returned by :ref:`psi.tvm.instructions.alloca` or :ref:`psi.tvm.instructions.alloca_const`.

load
""""

``load {ptr}``
  
Load a value from memory into a virtual register.
``{ptr}`` may be a pointer to any type.

``{ptr}``
  A value which is a pointer.

memcpy
""""""

``memcpy {dest} {src} {count} [{alignment}]``

Copy a sequence of values from one memory location to another.

``{dest}``
  Destination pointer. Must be a pointer type.
``{src}``
  Source pointer. Must have the same type as ``{dest}``.
``{count}``
  Number of elements to copy.
``{alignment}``
  Alignment of ``{dest}`` and ``{src}``.
  This will be ignored unless it is larger than the minimum alignment of
  the type pointed to by ``{dest}``, and defaults to 0.

.. note:: Use of this in TVM input is discouraged. 
  It exists to facilitate :ref:`type lowering <psi.tvm.type_lowering>`.

phi
"""

``phi {type}: {block} > {value}, ...``

Merge incoming values from different execution paths to a single name.
This is a Φ node of SSA form.

``{type}``
  Result type of this instruction

``{block} > {value}``
  A list of pairs of block labels and the value to be the result
  of this phi node on entering the current block from ``{block}``.

return
""""""

``return {value}``
  
Exit the current function, using ``{value}`` as the result of this function.

``{value}``
  Value to return from the current function.

.. _psi.tvm.instructions.solidify:

solidify
""""""""

``solidify {value}``

Assign a value to a phantom term for all instructions and blocks dominated by this instruction.
Phantom terms may arise in two ways: either as a phantom function parameter, or from the ``unwrap`` operation.

``{value}``
  The value the phantom term is to take on.
  The type of this value should be ``const {x}``, where ``x`` is a phantom value.

This instruction should be used with great care since it will circumvent the type check if ``value`` has
been obtained from uninitialized memory.
If the memory has been initialized with the correct type the type system then we know that ``value``
has the correct value since only a single value will be valid for that type.

store
"""""

``store {value} {dest}``
  
Write a value from a virtual register to memory.

``{value}``
  Value to be stored to memory.
``{dest}``
  Memory location to write to.
  If ``{value}`` has type ``{ty}``, ``{dest}`` must have type ``pointer {ty}``.

unreachable
"""""""""""

``unreachable``

A placeholder instruction for code branches which cannot ever be executed for
some reason.
The optimizer may use the presence of this instruction to infer that a branch
is never taken, amongst other things.
