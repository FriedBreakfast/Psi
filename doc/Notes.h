/* -*- mode: html -*- */
/**

\page notes Design notes

\section nested_functions Nested functions

Nested functions should be implemented at a higher level, where the
data structures should also be optimized. The LLVM code generator must
support injecting value mappings into functions so that variables
passed by function nesting can be easily supported. This means that
individual expressions should not know which function they are part of
since this should not be directly checked, however they should know
whether they are global or not.

\section calling_convention Function calling convention

All functions must be convertible to a generic calling convention
dependent only on the number of parameters since they may be called
from code which doesn't know the parameter types. Therefore:

<ul>
<li>Return type: i8*</li>
<li>n+1 parameters of type i8*</li>
</ul>

When passing a pointer type to a function, the pointer is passed
directly rather than by reference. When a pointer is returned from a
function, the value of the pointer is returned, <em>and</em> the value
of the pointer must be written to the memory area passed for the
return data. Thus, when passing the pointer to another function the
return value is used, and when returning the pointer, memcpy() can be
used to transfer the data from the child to the parent return area.

\section type_variables

Need to add a way of introducing type variables in the low-level
assembler after dereferencing a pointer. The \ref insn_cast and \ref
pointer_offset instructions should also have their \ref type_class
parameters removed and replaced by direct type references: it's
impossible to be totally consistent in syntax at the assembler level.

\section virtual_functions

\verbatim
require List.

Definition vtable := forall (ri : list reverse_lookup), function_pointer int ((pointer interface ri)::nil Set).

Definition interface := forall (ri : list reverse_lookup) (rv : list reverse_lookup), pointer (vtable ri) rv.
\endverbatim

\section terms_and_instructions Terms and instructions

There are three types of term:

<ul>
<li>Functional</li>
<li>Recursive</li>
<li>Instruction</li>
<li>Phi</li>
</ul>

Functional terms should support identical term unification. This can
allow existing terms to be modified, however before this can be done a
check must be made as to whether any previous matching terms exist.

Recursive terms are required to allow construction of self-references
in types.

Instruction terms can have their parameters modified freely. Phi terms
should only have parameters added, and may be unified later.

 */
