LLVM implementation notes
=========================

Calling conventions
-------------------

Getting LLVM to follow a calling convention is a bit tricky since it doesn't
actually do so automatically just by setting up function types.
It's also not really documented anywhere exactly what to do.
I think the correct way to do it is basically:

  1. Determine how an argument should be passed according to the platform ABI.
  2. If it's passed in registers, pass one argument of an appropriate type
     per register.
  3. If it's passed by value on the stack, store the value to the stack
     and pass it using a pointer argument with the ``byval`` attribute.
  4. If it's passed by reference, just copy the source value if required and
     pass the pointer explicitly.
     
This procedure so far isn't exactly complete.
LLVM will pass arguments on the stack if it runs out of registers so sometimes
that can be exploited to avoid ``byval``.
It's also not clear what the ``inreg`` attribute is for, at least on x86 an x86-64.

I think that LLVM regards struct types as equivalent to an unpacked list of its
members for purposes of parameter passing, which would certainly be sensible
since there's no other way of handling multiple return values.

x86
"""

* Floating point values are returned in ST0 *unless* ``inreg`` is set in which case
  they are returned in XMM0.
* LLVM will return multiple values in EAX:EDX:ECX. I don't know of any calling conventions
  which return data in ECX, so presumably anythin which returns more than 3 elements of data
  must do so using ``sret``.
* The ``inreg`` attribute on floating point parameters causes them to be passed in SSE registers
  XMM0-2.