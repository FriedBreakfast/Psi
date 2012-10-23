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
Second, there is only a single way to reuse computed values: using the :psi:`block <Psi::Compiler::Block>`, :psi:`statement <Psi::Compiler::Statement>`
and :psi:`statement reference<Psi::Compiler::StatementRef>` trees.
Otherwise, if a term appears in two different places it will be evaluated twice (if two copies of a block are in scope this is an error).

.. _psi.internals.trees.result_types:

Result types
------------

The rules for tree evaluation make the result types of terms slightly non-trivial.
Essentially, if the type of an expression depends on tree which requires stateful evaluation then this value
must be replaced by a wildcard (wrappped by an exists tree) in the resulting type.
To give an example in pseudocode::

  x : 3;
  y : make_array(int,x);
  
Although ``y`` does indeed have length 3, it will have a type like ``exists (a:int -> array(int,a))``,
because as ``x`` is a variable re-evaluating it may give a different result.
This also means that the array elements in this case would have to be stored on the heap because the length of a stack allocation cannot depend on a wildcard.
This can be fixed by using::

  x :: 3;
  y : make_array(int,3);
  
``::`` indicates a definition rather than an assignment, so ``x`` cannot be modified and now ``y`` will have a type like ``array(int,x)``,
and the array elements can therefore be stored on the heap until ``x`` goes out of scope.
The trick is that the number 3 in this example may be replaced by a variable, in which case the value is frozen at the point of definition.
