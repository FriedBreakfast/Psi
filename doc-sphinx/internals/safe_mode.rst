Safe mode
=========

It is quite straightforward to implement a "safe" subset of instructions.
The limitations are

  1. All objects which are passed between functions must be allocated on the heap rather than
     the stack, under a garbage collector, unless it can be shown that they have fixed lifetime.
  2. Pointer casts and pointer arithmetic cannot be used.
  3. Unions cannot be used; the type of data that memory holds must be fixed at allocation time.
     This is basically the same as disallowing pointer casts.
  4. Calls to external functions are intrinsically unsafe; the safe-unsafe boundary must be explicitly
     marked and unsafe functions callable from safe code should be explicitly allowed.

Other than that, array element access must be bounds checked but that's not much of a limitation
since the array length is always available from the type system unless it's a phantom value.
Even flexible array members can be allowed provided their length is stored as a ``const``
value at allocation time.

I'd like mixing of safe and unsafe code to be relatively painless from the unsafe side;
this will make design of the garbage collector rather tricky. Ideally, the garbage collector will
not need to pause threads that do not manipulate any data structures which are either garbage collected
themselves or contain references to garbage collected objects, so that 'stop-the-world' would actually
become only 'stop-the-safe-world'.

Mixed mode
----------

The case of a ``std::vector`` or ``java.lang.ArrayList`` type container is rather interesting.
In C++ it is assumed that ``std::vector`` can reallocate its storage when any elements are inserted
or removed, and it is the users responsibility to ensure that no pointers are held into the vector
elements when this occurs.
In Java, the ``ArrayList`` class avoids this problem by not allowing the user direct access to
the array elements.

Of course, I'd like to allow direct access since the elements of such a container should be allowed
to be complex objects rather than restricted to pointers as they are in Java.
This raises the question of what behaviour to implement in safe mode when the vector is resized.
Clearly the old memory cannot be freed whilst references into it are still held since this would
break the safety guarantee, however it doesn't really make sense for the user to access old objects
this way (since they are expected to be in a null state as move assignment will be used).

I don't think these problems are completely solvable.
Essentially any pointers into the vector are going to be impossible to keep track of.
Iterators however are another matter.
It is possible to provide two separate implementations of array list iterators; an unsafe version
which assumes the user has obeyed the interface requirements and a safe one which verifies them.
This is not the same as providing two functions since the safe iterators will require more data,
they must verify that the vector has not reallocated its memory (or become too short).

I think the solution to this has to be to create a more complex vector implementation. Basically
the conversion of a raw pointer to a safe (garbage collected) pointer has to be an unsafe operation,
and it is the responsibility of the code performing the conversion to ensure that the memory referenced
now falls under the purview of the garbage collector and will not be freed manually.
Of course given that raw references may also exist the garbage collector will have to cooperate with
manual memory management to some extent in order to ensure that it does not release memory before
all raw references are destroyed.

Dynamic cast
------------

Both C++ and Java have a checked cast operator. C++ cannot be made safe for other reasons, but it seems
worthwhile to allow a Java-like dynamic cast operation to be performed safely.
This requires at least two new TVM values:

1. ``element_offset {a} {up_a} {b} {up_b}``

  Offset from an element of type ``{a}`` to an element of type ``{b}``.
  ``{up_a}`` and ``{up_b}`` are upward reference chains associated with ``{a}`` and ``{b}`` respectively.
  Note that this is a type, not a value, and it behaves similarly to ``const``.
  
  Three other operations are required to work with this type:
  
  ``element_offset_v {ty} :: element_offset {ty} upref_null {ty} upref_null``
  
  This constructs the basic element offset, from an object to itself (the offset is zero).
  ``left_offset`` and ``right_offset`` are then required to modify the left and right elements,
  respectively.
  
2. ``type_id {t}``

  A type with no constructor. It can only be created as a global variable, which must have a unique address,
  and therefore can be used to check if two types are the same via
  
  ``tcjmp {a} {b} {yes} {no}``
  
  ``{a},{b}`` are of type ``pointer type_id ?``. If the two pointers compare equal this instruction jumps to
  the ``{yes}`` block and both types are considered equivalent that block and all blocks dominated by it.
  Otherwise it jumps to the ``{no}`` block.
  

Design issues
-------------

* Should safe code be able to access unions created (and passed to the safe code) by unsafe code?
  Providing that safe code cannot allocate unions it cannot break safety on its own. However, it
  doesn't really make sense to allow access to unsafe data structures from safe code since that
  rather circumvents the point of it being safe code in the first place.

* I don't want to explicitly mark safe data structures, since they are the same as unsafe data structures
  in all respects (except that they cannot contain unions); it is code that is safe or unsafe.
  
* I should explicitly enumerate the requirements unsafe code must meet in order to interact with
  safe code without potentially making it unsafe. Unsafe code will be able to write invalid pointers
  to data structures accessed by safe code and the safe code will not check them.

* I think this should be implemented as a translation stage in TVM before aggregate lowering. Since
  exception handling isn't yet implemented, aggregate lowering is the only stage but I think the
  eventual "standard" compilation stages will be:
  
    Safety check and checked instruction replacement → Exception control flow expansion → Aggregate lowering