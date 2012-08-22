Instructions
============

Note that the notation ``{name}`` denotes an expression, ``{`` and ``}`` are not part of the syntax.

Functional operations
---------------------

Types
^^^^^

Primitive types, and operations for constructing aggregate types.

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
  It primarily exists to facilitate :ref:`type lowering <psi-tvm-type_lowering>`.

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

pointer
"""""""

``pointer {type}``

``{type}``
  Pointed-to type.
  
A reference to a value in memory of type ``{type}``.

struct
""""""

``struct {members} ...``

``{members}``
  A list of types which are members of this struct type.
  
A data type containing a sequence of elements of different types.

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

union
"""""

``union {members} ...``

``{members}``
  A list of types which are members of this union type.
  
A data type holding a value of one of its members.
Note that type equivalence for union types is based on *exact* equivalence of
the ``{members}`` list; two lists which produce the same memory layout but are
different (e.g. by having an element repeated) are considered different.

Note that the old C-style trick for getting parts of integers::

  %s = array i8 #up4
  %t = union i32 %t
  %a = union_v %t #i432098
  %b = array_el (union_el %t %s) #up1

Is valid even at the virtual register level.
Obviously the results are not portable though.

Aggregate operations
^^^^^^^^^^^^^^^^^^^^

Operations for constructing and manipulating aggregate types in virtual registers,
and manipulating pointers to aggregate types.

empty_v
"""""""

``empty_v``

Value of the empty type.

pointer_cast
""""""""""""

pointer_offset
""""""""""""""

specialize
""""""""""

struct_el
"""""""""

struct_ep
"""""""""

struct_v
""""""""

union_el
""""""""

union_ep
""""""""

union_v
"""""""

Arithmetic
^^^^^^^^^^

Global constants and numerical expressions.
Note that numerical constants are covered in :ref:`psi-tvm-numerical_constants`

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

alloca
^^^^^^

``alloca {type} [{count} [{alignment}]]``

``{type}``
  Type to allocate space for.
  The result type of this instruction is ``pointer {type}``.
``{count}``
  Optionally, the number of elements to allocate. Defaults to 1.
``{alignment}``
  Optional alignment specification. This will be ignored if it is smaller
  then the minimum alignment for ``{type}``. Defaults to 0.

Allocate storage for a type on the stack.

br
^^

``br {block}``

``{block}``
  Name of a block in this function.
  
Jump to the specified block.
Note that block names are literals, the destination block cannot be
an expression selecting a block because in that case the flow of
control cannot be tracked. To conditionally select a branch target,
see :ref:`psi-tvm-instruction-cond_br`

call
^^^^

``call {target} {args} ...``

``{target}``
  Address of function to call.
  This must have pointer-to-function type.
``{args}``
  A list of arguments to pass to the function.
  
Invoke a function.

.. _psi-tvm-instruction-cond_br:

cond_br
^^^^^^^

``cond_br {cond} {iftrue} {iffalse}``

``{cond}``
  ``bool`` value used to select the jump target.
``{iftrue}``
  Block to jump to if ``{cond}`` is true.
``{iffalse}``
  Block to jump to if ``{cond}`` is false.
  
Continue execution at a location dependent on a boolean value.

load
^^^^

``load {ptr}``

``{ptr}``
  A value which is a pointer.
  
Load a value from memory into a virtual register.
``{ptr}`` may be a pointer to any type.

memcpy
^^^^^^

``memcpy {dest} {src} {count} [{alignment}]``

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

Copy a sequence of values from one memory location to another.

.. note:: Use of this in TVM input is discouraged. 
  It exists to facilitate :ref:`type lowering <psi-tvm-type_lowering>`.

phi
^^^

``phi {type}: {block} > {value}, ...``

``{type}``
  Result type of this instruction

``{block} > {value}``
  A list of pairs of block labels and the value to be the result
  of this phi node on entering the current block from ``{block}``.

Merge incoming values from different execution paths to a single name.
This is a Φ node of SSA form.

return
^^^^^^

``return {value}``

``{value}``
  Value to return from the current function.
  
Exit the current function, using ``{value}`` as the result of this function.

store
^^^^^

``store {value} {dest}``

``{value}``
  Value to be stored to memory.
``{dest}``
  Memory location to write to.
  If ``{value}`` has type ``{ty}``, ``{dest}`` must have type ``pointer {ty}``.
  
Write a value from a virtual register to memory.

unreachable
^^^^^^^^^^^

``unreachable``

A placeholder instruction for code branches which cannot ever be executed for
some reason.
The optimizer may use the presence of this instruction to infer that a branch
is never taken, amongst other things.
