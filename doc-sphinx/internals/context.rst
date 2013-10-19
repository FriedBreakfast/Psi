Contexts
========

It seems a good idea to implement some sort of context dependent syntax.
In particular, the following cases arise:

* Nested funtions: the inner function should be able to access variables
  from the outer function, although as in C++11 some user control over
  how this is done is required.

* Interface implementations inside functions: much the same as nested
  functions, but requires multiple inner function contexts to be merged
  together.
  
* Member functions.
  
* Mutually recursive nested functions.

* Nested types (inside functions at least): need to pick up outer functional
  parameters and add them to the type parameter list.
  
* Functions and function types inside interface definitions and implementations.

To avoid fixing the structure of contextual data, two problems must be solved:

* How to pass contextual data in a generic way: this should be attached to :psi:`EvaluateContext`
  and searched via the usual overload resolution mechanism.

* What's the best way to represent context required by the situations above?
  There seem to be two distinct cases:
  
  1. Nested functions. These can have context built automatically and unified
     between multiple nested functions.
     
  2. Interface definitions. Need to automatically add interface parameter.
     Syntax should also be adjusted for interface definition.
     
  3. Add a "declaration" parser. This should allow the context to select an
     action based on what sort of thing appears to be being declared:
     a type, value or function. This will also involve overloading,
     but should only occur at the first level of parsing.
     
Modify the expression handling system: for context declarations, go through
an alternative interface which defaults to the current context-free behaviour.
