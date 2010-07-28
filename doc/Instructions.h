/* -*- mode: html; coding: utf-8 -*- */
/**

\page instructions Instruction set

<ol>
<li>\ref intrinsic_types<ol>
 <li>\ref type_class</li>
 <li>\ref type_pointer</li>
 <li>\ref type_integer</li>
 <li>\ref type_member</li>
 <li>\ref type_reverse_member</li>
</ol></li>
</ol>

\section instruction_arguments Instruction arguments

Arguments may be of several types:

<ul>
<li><tt>%%name</tt>: local and global variables, constants, functions and labels.</li>
<li><tt>type[arguments] value</tt>: constructor for type constants.</li>
</ul>

\section Built in types

<ul>
<li>Pointer: pointer to arbitrary data. Takes two type parameters: the
type pointed two and a list of parent structures for reverse member
lookup (via the \ref insn_reverse_member instruction).
<ul>
<li>LLVM mapping: best approximation or <tt>i8*</tt> by
default</li>
<li>Syntax: <tt>P[a]</tt> or <tt>P[a,b]</tt>: \c a is the target type
and \c b is a parent structure list.</li></ul>
</li>
<li>Size: integer which is at least as large as the largest
allocatable block of memory. Usually this will be the same size as a
pointer.
<ul>
<li>LLVM mapping: <tt>%%size = type iN</tt> of appropriate width</li>
<li>Syntax: <tt>S</tt></li></ul>
</li>
<li>Definitions: carry information required to index address a
specified type. Currently this is \c sizeof and \c alignof
<ul><li>LLVM mapping: <tt>%%def = type {%%size, %%size}</tt></li>
<li>Syntax: <tt>C[a]</tt>: \c a is the class type.</li></ul></li>
<li>Array
<ul><li>LLVM mapping: <tt>[t x n]</tt></li>
<li>Syntax: <tt>A[t,n]</tt>: \c t is the element type and \c n is the length.</tt></ul>
</li>
<li>Pointer to member
<ul><li>LLVM mapping: <tt>%%size</tt>, however this will be a frequent
candidate for optimization since element lookup instructions should
use the <tt>getelementptr</tt> with the index of the </li>
<li>Syntax: <tt>PM[p,c,n]</tt>: \c p is the parent type, \c c is the
child type and \c n is the <em>list</em> of indices of the child in
the parent. In the case of unions and structures \c c and \c n
indicate the exact member.</li></ul>
<li>Compile time pointer to member
<ul><li>LLVM mapping: none</li>
<li></li></ul>
<li>Type list
<ul><li>LLVM mapping: none, since it has no data representation.</li>
<li>Syntax: <tt>(a,b,c,...)</tt> or <tt>L[a,L[b,L[c,E]]]</tt>, where
\c E is end-of-list.</li></ul>
</li>
<li>Compile time integer
<ul><li>LLVM mapping: none, since it is a type which defines a
value.</li>
<li>Syntax: the relevant number.</li></ul>
</li>
<li>Integers, floating point, etc...
<ul><li>LLVM mapping: whatever</li></ul></li>
</ul>

\section Data structure walking and pointer type

I want a certain ability to perform reverse member lookups through
pointers: this allows implementing a base/derived class relationship
generically (without making the type system control the layout) in a
type-safe way. In order to do this the pointer type is imbued with a
two-argument constructor: the first is the pointed-to type, and the
second is a reverse lookup list.

In a full-on proof-theoretic functional system, I think this would
require implementing data structures as pairs (with two composition
operators: ∧ and ∨), and then define different reverse lookup
possibilities for the four different member access types (left/right,
and/or).

In practise I've created the member data type, which is <tt>M a b
c</tt>, where \c a is the parent type, \c b is the child type and \c c
is the index of the child in the parent.

For more complex member lookup scenarios, pointer to members and
pointer offsets should also be supported, however these will not
support reverse lookup and will return a pointer whose second argument
is existentially quantified (i.e. unknown to the caller), and
therefore cannot be walked in reverse.

\section instructions_member_access Member access

\subsection insn_member member

\c struct, \c union, \c array and \c pointer member access.

<tt>member value index</tt>

Gets a reference to a member of a structure. \c index is a member
reference. \c value should be a struct, union or pointer.

\verbatim
member :: P a b -> M c a d -> P d (R (M c a d) b)
\endverbatim

\subsection insn_reverse_member reverse_member

Opposite of the \ref insn_member instruction.

<tt>reverse_member value index</tt>

\verbatim
reverse_member :: P a (R (M b c a) d) -> P c d
\endverbatim

It's debatable whether the second argument should be included since
it's implied by the first.

\subsection insn_array_member array_member

Computes a \ref type_member object for performing array member
lookups.

<tt>array_member type index</tt>

\verbatim
array_member :: a -> n -> M (A a n) a n
\endverbatim

\subsection insn_gep gep

GEP instruction from LLVM - used for pointer offsets since \ref
insn_member is not appropriate.

\subsection insn_cast cast

<tt>cast ptr type</tt>

\verbatim
cast :: P a b -> C c -> P c e
\endverbatim

\subsection insn_define define

<tt>define type</tt>

Informs the local context of the size and alignment (the exact
information is implementation dependent, but must include these two)
of a given type. Obviously this violates type safety and must be used
with care. The type which the compiler is being informed about
<em>must</em> be a free type variable (i.e. not a known composite of
unknown types) and must not have previously been defined.

\verbatim
define :: C a -> Void
\endverbatim

\section instructions_memory Memory access

Memory access instructions require that the types being loaded/stored
are fully specified, so their memory layout is known.

\subsection insn_load load

\verbatim
load :: P a b -> a
\endverbatim

\subsection insn_store store

\verbatim
store :: P a b -> a -> Void
\endverbatim

\section instructions_control_flow Control flow

\subsection instruction_ret ret

\verbatim
ret :: t -> End
\endverbatim

\subsection instruction_jump jump

Conditional jump instruction.

\verbatim
jump :: i1 -> label -> Void
\endverbatim

\subsection instruction_unwind unwind

Unwind the stack.

\verbatim
unwind :: End
\endverbatim

\subsection instruction_call call

Invoke a user-defined function. This does not have a general type
signature since the type will depend on the user function.

\verbatim
call f(...) unwind label
\endverbatim

*/
