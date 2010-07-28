/**
\page typesystem Type system design

\section Unification

Type unification occurs when either the ?: operator or an if/then/else
clause is used. It should be implemented by:

\verbatim
class Merge a b -> c:
 merge_left :: a -> Class b -> c
 merge_right :: Class a -> b -> c
\endverbatim

So that there is a class which defines the result \em type of the
merge, and this class supplies two functions which convert either one
type or the other, plus an argument specifying the type of the
unavailable value, to the result type.

There are two problems here:

<ul>

<li>The type without a value does not really have well defined
semantics: in most cases it should be possible to get type parameters
but it's not obvious how to do this since they may be unavailable in
some cases (where the type variable comes from a scope which did not
run).

<ul> <li>However, only certain types support existential parameters,
such as pointers. Therefore, the \c Class objects passed to these
functions should really be a special case and have some sort of \c
parameterizedTypes member. </ul> </li>

<li>The type without a value should be more user friendly: they should
have members like \c Array.getElementType not \c
Class.getParameter(0), for example.</li>

</ul>

\section Blocks

The result of each instruction may introduce new type variables into
the remainder of the block. This will interact with the type merging
and require some support for existential types.

\section Class_objects Class objects

Class objects should be approximately:

\code
template<typename T>
struct Class {
  size_t length;
  size_t align; /// Required at least for allocating return value storage
  void (*move) (Class<T> *cls, T *target, T *source);
  void (*move_construct) (Class<T> *cls, void *target, T *source);
  void (*destroy) (Class<T> *cls, T *self);
  std::shared_ptr<typename T::DataType> data;
};
\endcode

There should also be additional type-specified data for parameterized
types.

\section Member_lookup Member lookup

There is no problem with defining a global function such as:

\verbatim
getArrayElementType :: T,N => Class Array T N -> Class T
\endverbatim

However this should appear as a member of <tt>Class Array ? ?</tt>. In
order for this to work, the global compile-time lookup mechanism must
allow this type of pattern matching.

\section Pattern_matching Pattern matching

\section Argument_passing Function argument passing

Function arguments will be passed by value when they meet the
following criteria:

<ul>
<li>The type is fully known at compile time</li>
<li>The type has a trivial move constructor and trivial destructor</li>
<li>The size of the object may also be considered</li>
</ul>

Otherwise it will be passed by reference.

\section Basic_types Basic types

Primitive types:

<ul>
<li><tt>intN</tt> (as LLVM)</li>
<li><tt>uintN</tt> (as LLVM)</li>
<li><tt>float</tt>, <tt>double</tt> (as LLVM)</li>
<li><tt>char</tt> (32-bit)</li>
<li><tt>bool</tt> (8-bit)</li>
</ul>

Could also include <tt>size_t</tt> and <tt>ptrdiff_t</tt>.

Derived types:

<ul>
<li>Struct</li>
<li>Union</li>
<li>Array</li>
<li>Pointer</li>
<li>Function pointer</li>
</ul>

I think function pointers should be raw: however, this means that the
function pointer signatures will have to include pointers to
interfaces which are already known (since the types have been fixed),
and I'll have to allow binding those parameters some other way. In
practice this will mean that user-visible function pointers will not
be primitive since they need to support bound parameters.

\section Virtual_functions Virtual functions

I want to be able to implement virtual functions with the same
efficiency as C++, without the burdensome type system (virtual
inheritance etc.).

In order to implement this I need a \c reverse_member instruction,
allowing turning a pointer to a member of a structure into a pointer
to the structure itself.

\c reverse_member cannot be supported for arrays because that would
require that each member of the array had a different type.

<ul>
<li>Basic interface (reverse offset performed by member functions)
\verbatim
struct[T, Member] VTable {
  T (*value) (Implementation@Member);
}

struct[T, Member] Interface {
  VTable[T, Member] *vtable;
}

function[a,b] work(Interface[a,b]@b *impl) -> a {
  return impl->vtable->value(impl);
}

struct Implementation {
  Interface[int, Implementation.value] base;
  int value;
}

function value_implementation(Interface[int, Implementation.value]@Implementation.value self) -> int {
  self2 = reverse_member self;
  return self2->value;
}
\endverbatim
</li>
</ul>

*/
