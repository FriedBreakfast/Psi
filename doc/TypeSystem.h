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

\verbatim
template<typename T>
struct Class {
  size_t length;
  void (*move) (Class<T> *cls, T *target, T *source);
  void (*move_construct) (Class<T> *cls, void *target, T *source);
  void (*destroy) (Class<T> *cls, T *self);
};
\endverbatim

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



*/
