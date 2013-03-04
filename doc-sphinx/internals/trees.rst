.. _psi.internals.trees:

Trees
=====

Internally, code and data is represented during compilation by trees (base class :psi:`Psi::Compiler::Tree`).
Code which is finallly compiled is represented mostly by terms (base class :psi:`Psi::Compiler::Term`),
which are defined as those trees which can generally be used as a value.

Evaluation order
----------------

Each term is responsible for the order its members are evaluted in (and whether they are in fact evaluated).
However, generically some remarks can still be made.
First when a term is evaluated it is completely evaluated before another term is evaluated; evaluation of two terms will not be interleaved.
This means that some subtleties of C++ argument evaluation are avoided: an expression which allocates memory and creates a smart pointer is safe
because the smart pointer creation will immediately follow memory allocation, rather than some other operation in the function argument list.
There are two ways to reuse computed values: using the :psi:`block <Psi::Compiler::Block>` and :psi:`statement <Psi::Compiler::Statement>` trees,
or the :psi:`functional evaluation <Psi::Compiler::FunctionalEvaluate>` and :psi:`functional reference<Psi::Compiler::FunctionalReference>`.
Otherwise, if a term appears in two different places it will be evaluated twice (if two copies of a block are in scope this is an error).

Variable scoping
----------------

There are two distinct types of variables which can be represented by trees: functional variables and stateful variables.
References (both l-value and r-value) count as functional variables, because they are internally equivalent to functional pointers.
The scoping behaviour of these two types of variable is different.

Stateful variables are those which are stored in memory and can be modified after their initial assignment.
They may have a user-specified constructor and destructor.
These variables are explicitly scoped: their constructor is called when a value is assigned, and they remain in scope until one of the following:

  1. Until the tree it is part of is converted to a functional value.
  2. Until the tree it is part of is stored to a named variable in a block
  3. If it is a named variable in a block, until the end of that block.
  4. If it is part of the result value of a block, until the end of that block.

When a stateful variable goes out of scope, its destructor is run and its stack space is reclaimed.

Functional variables are stored in virtual registers and may not have user specified constructors and destructors.
They remain in scope as long as the point as long as they dominate the current block,
that is at which they are defined is guaranteed to have run before the current evaluation point.
