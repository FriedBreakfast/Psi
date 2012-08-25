Instructions
============

Note that the notation ``{name}`` denotes an expression, ``{`` and ``}`` are not part of the syntax.

Functional operations
---------------------

Types
^^^^^

Primitive types, and operations for constructing aggregate types.

.. _psi.tvm.instructions.base:

base
""""

``base {derived} {offset} > {type}``

Base type.
This allows for single dispatch to be implemented.

``{derived}``, ``{offset}``
  Variable identifiers used in ``{type}`` to refer to the derived type and the
  offset from the derived type to the base type respectively.
``{type}``
  Data visible in the base type.

.. _psi.tvm.instructions.base_ptr:

base_ptr
""""""""

``base_ptr {base}``

Pointer to a parameterized type.

``{base}``
  Base type, must be a :ref:`psi.tvm.instructions.base`.

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

member
""""""

``member {type1} {type2}``

Pointer-to-member type.
This allows a pointer to a ``{type2}`` to be obtained from a pointer to a ``{type1}`` using an offset in a type safe way.

``{type1}``
  A type which at some depth should contain ``{type2}``.
``{type2}``
  A type which is a member of ``{type1}``.

.. _psi.tvm.instructions.member_ptr:

member_ptr
""""""""""

``member_ptr {member}``

An interior pointer to a data structure.
This is a restricted variation of a pointer which allows retrieval of the outer data structure from the inner one given the offset.

``{member}``
  Must be a value of type ``member``.

.. _psi.tvm.instructions.pointer:

pointer
"""""""

``pointer {type}``
  
A reference to a value in memory of type ``{type}``.

``{type}``
  Pointed-to type.

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

Aggregate operations
^^^^^^^^^^^^^^^^^^^^

Operations for constructing and manipulating aggregate types in virtual registers,
and manipulating pointers to aggregate types.

apply
"""""

``apply {recursive} {parameters...}``

Specialize a recursive type.

``{recursive}``
  A ``recursive`` type or value.
``{parameters...}```
  A list of parameters to specialize the generic type with.

  .. _psi.tvm.instructions.unbase:

derived
"""""""

``derived {ptr}``

Get the derived type of a :ref:`psi.tvm.instructions.base_ptr`.
See :ref:`psi.tvm.instructions.unbase`.

``{ptr}``
  Base pointer, of type :ref:`psi.tvm.instructions.base_ptr`.

empty_v
"""""""

``empty_v``

Value of the empty type.

member_apply
""""""""""""

``member_apply {ptr} {member}``

Use a member pointer to get a pointer to the inner value from the outer value.

``{ptr}``
  A pointer, of type ``pointer {t1}``.
``{member}``
  A pointer to member, which must be of type ``member {t1} {t2}``.
  
The result of this operation is a ``pointer {t2}``.

.. _psi.tvm.instructions.member_apply_ptr:

member_apply_ptr
""""""""""""""""

``member_apply_ptr {ptr} {member}``

Use a member pointer to get a ``member_ptr`` from the outer value.

``{ptr}``
  A pointer, of type ``pointer {t1}``.
``{member}``
  A pointer to member, which must be of type ``member {t1} {t2}``.

The result of this operation is a ``member_ptr {member}``.

member_combine
""""""""""""""

``member_combine {m1} {m2}``

Combine two member pointers to a single one.

``{m1}``
  First member pointer.
  This should have type ``member {t1} {t2}``.
``{m2}``
  Second member pointer.
  This should have type ``member {t2} {t3}``.
  
Given the types of each parameter, the result of this operation will have type ``member {t1} {t3}``.

.. _psi.tvm.instructions.member_inner:

member_inner
""""""""""""""""

``member_inner {mp}``

Take a ``member_ptr`` and return a pointer to the inner type.

``{mp}``
  A ``member_ptr`` value.
  
If ``{mp}`` has type ``member_ptr {member}``, and then ``{member}`` has type ``member {t1} {t2}``,
``member_inner {mp}`` has type ``pointer {t2}``.
This operation works (produces a non-phantom result) even if ``{member}`` is a phantom value.

.. _psi.tvm.instructions.member_outer:

member_outer
""""""""""""""""

``member_outer {mp}``

``{mp}``
  A ``member_ptr`` value.

If ``{mp}`` has type ``member_ptr {member}``, and then ``{member}`` has type ``member {t1} {t2}``,
``member_inner {mp}`` has type ``pointer {t1}``.
Note that it will often be the case that ``{member}`` is a phantom value, since this mechanism is present to implement :ref:`virtual functions <psi.tvm.virtual_functions>`.
In this case the result of this operation will also be a phantom value.

pointer_cast
""""""""""""

``pointer_cast {ptr} {type}``

Cast a pointer to a different pointer type.
The result type of this instruction is ``pointer {type}``.

``{ptr}``
  Pointer to be cast.
  The result of this operation points to the same address as ``{ptr}``.
``{type}``
  Type to cast the pointer to.
  Note that this is not a pointer type itself unless the result is a pointer to a pointer.

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

specialize
""""""""""

struct_el
"""""""""

struct_ep
"""""""""

struct_v
""""""""

.. _psi.tvm.instructions.unapply:

unapply
"""""""

``unapply {ptr}``

Turn a pointer to a specialized type into a generic pointer.

``{ptr}``
  Pointer to a ``recursive`` type.

If ``{ptr}`` is ``apply {r} ...``, the result will a ``base_ptr {r}``.

unbase
""""""

``unbase {ptr}``

Unwrap a :ref:`psi.tvm.instructions.base_ptr` to give a :ref:`psi.tvm.instructions.member_ptr`.
See :ref:`psi.tvm.instructions.derived`.

``{ptr}``
  A :ref:`psi.tvm.instructions.base_ptr`.

.. _psi.tvm.instructions.derived:

undef
"""""

``undef {type}``

Undefined value of any type.
The compiler is allowed to make any assumption whatsoever about the contents of such a value.

``{type}``
  Result type of this operation.

union_el
""""""""

union_ep
""""""""

union_v
"""""""

zero
""""

``zero {type}``

Zero-initialized value of any type.

``{type}``
  Result type of this operation.
  

Arithmetic
^^^^^^^^^^

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

alloca
^^^^^^

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

br
^^

``br {block}``

Jump to the specified block.

Note that block names are literals, the destination block cannot be
an expression selecting a block because in that case the flow of
control cannot be tracked. To conditionally select a branch target,
see :ref:`psi.tvm.instructions.cond_br`

``{block}``
  Name of a block in this function.
  
call
^^^^

``call {target} {args...}``

Invoke a function.

``{target}``
  Address of function to call.
  This must have pointer-to-function type.
``{args...}``
  A list of arguments to pass to the function.

.. _psi.tvm.instructions.cond_br:

cond_br
^^^^^^^

``cond_br {cond} {iftrue} {iffalse}``
  
Continue execution at a location dependent on a boolean value.

``{cond}``
  ``bool`` value used to select the jump target.
``{iftrue}``
  Block to jump to if ``{cond}`` is true.
``{iffalse}``
  Block to jump to if ``{cond}`` is false.

load
^^^^

``load {ptr}``
  
Load a value from memory into a virtual register.
``{ptr}`` may be a pointer to any type.

``{ptr}``
  A value which is a pointer.

memcpy
^^^^^^

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
^^^

``phi {type}: {block} > {value}, ...``

Merge incoming values from different execution paths to a single name.
This is a Φ node of SSA form.

``{type}``
  Result type of this instruction

``{block} > {value}``
  A list of pairs of block labels and the value to be the result
  of this phi node on entering the current block from ``{block}``.

return
^^^^^^

``return {value}``
  
Exit the current function, using ``{value}`` as the result of this function.

``{value}``
  Value to return from the current function.

store
^^^^^

``store {value} {dest}``
  
Write a value from a virtual register to memory.

``{value}``
  Value to be stored to memory.
``{dest}``
  Memory location to write to.
  If ``{value}`` has type ``{ty}``, ``{dest}`` must have type ``pointer {ty}``.

unreachable
^^^^^^^^^^^

``unreachable``

A placeholder instruction for code branches which cannot ever be executed for
some reason.
The optimizer may use the presence of this instruction to infer that a branch
is never taken, amongst other things.
