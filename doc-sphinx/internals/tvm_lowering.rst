TVM lowering
============

This document contains notes about the conversion from :ref:`trees <psi.internals.trees>` to :ref:`psi.tvm` values.

Type lowering
-------------

Even though types and values are treated on a largely equivalent footing by trees, this is not the case during lowering.
Some of the reasons for this are described in the section on :ref:`tree result types <psi.internals.trees.result_types>`;
essentially, a type may depend on a value which goes out of scope.
Because of this sort of situation, type parameters may contain unknown values, and in this case the TVM lowering routines
must be able to distinguish those types whose layout does not depend on such parameters from those whose layout does,
as this is an error.

Because of this generic type lowering must be performed in two passes: in the first pass the TVM type tree is built and
in the second pass the dependence of the type layout on its parameters is established.

Constructor calls
-----------------

Constructors are called the smallest number of times possible, that is an expression::

  a : f(...)
  
Will `not` create a temporary of the result of ``f`` and then move the value into ``a``,
rather, it will directly construct the result into ``a``.
Move constructor calls may be freely elided by optimisation,
however the main purpose of the implementation is that a move constructor is not necessary
in many situations, and this is the principal way to construct non-moveable types.

Module attachment
-----------------

Global symbol are explicitly attached to modules by a reference to their containing module.

Global variable initialisation
------------------------------

Global variable initialisation is done on a "best effort" basis.
That is, any if a global variable A depends on a global variable B (including the transitive closure of dependencies),
B will be initialised before A, and vice versa.
If both A depends on B and B depends on A, then a common function is generated which initialises both by first
default constructing both, and then re-assigning them using proper initialisers.
This ensures that a valid value is visible at all times, but does mean that modifications performed by the initialiser
of A on B may or may not be visible after both are initialised.
This extends to larger sets or interdependent variables.

Within these interdependent sets, there may be at most one variable whose type is not MoveAssignable, otherwise an error occurs.
The variable with this type will be constructed after default construction of all other variables but before they are assigned their value.

Interface implementations
-------------------------

When an implementation is instantiated, a new copy must be generated with values filled in according to the parameter matches.
For globals, the LLVM ``unnamed_addr`` attribute (not yet implemented in TVM) should be used to enable merging of identical instances.
To instantiate an implemenation, wildcard values in the definition of the implemenation are replaced by the result of pattern matching.

In theory, interface implementations may be instantiated at each point of use;
in practice a mechanism to reduce duplicate instantiations should be used.

Nested functions
----------------

Nested functions are implemented using the :psi:`Psi::Compiler::Closure` tree.
The facilitates automatic generation of a type containing variables captured from enclosing scopes.
Recursion between nested functions is also handled; in this case it is necessary to merge the required variables from all functions in a graph.
The user may then generate local interface implementations from these values.

Nested generic types
--------------------

Generic types may implicitly depend on local variables.
The caveats about :ref:`tree result types <psi.internals.trees.result_types>` must be borne in mind,
that is they may only depend on frozen values otherwise the type definition would be unclear.
These are lowered by creating a global type with any extra variables transformed into parameters; this may include function nesting parameters.

Symbol naming
-------------

Symbol names are generated from :psi:`Psi::SourceLocation` values attached to trees.
For externally visible functions and variables, the name is generated according to the following Python code::

  def mangle(parts):
    return '_Y0%s' % ''.join('%d_%s' % (len(p), p) for p in parts)

Where ``parts`` is a list of enclosing namespaces and the variable name.
This is similar to the C++ name mangling convention used on Linux, and means that these symbol names are valid C identifiers (albeit implementation reserved).
Module-local symbols are the same except that a number may be appended for disambiguation.
The ``0`` after ``_Y`` is to allow for changes to this convention.
