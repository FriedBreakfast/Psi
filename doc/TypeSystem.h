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

\section Blocks

The result of each instruction may introduce new type variables into
the remainder of the block. This will interact with the type merging
and require some support for existential types.

*/
