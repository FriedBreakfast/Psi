Glossary
========

.. glossary::
  :sorted:

Phantom value
  *Phantom* values implement the mathematical notion of :math:`\exists x. f(x)`,
  playing the role of :math:`x` here. A phantom value cannot be used for any operation
  where its value can influence memory access; that is load, store and function calls
  (unless the function expects a phantom value as a parameter).
  
Recursive type
  A unique named type. It is called *recursive* because it may contain instances of
  differently-parameterised versions of itself as well as pointers to the same type.
  Note that these are required to be global, so that the possible dependence on
  function parameters and variables is known before the layout is specified.
  
  .. note:: Recursive types are not currently implemented as well as they might be.
    It ought to be possible to use them as the basis of a full functional system because
    they can also be used for values; so trying to implement the Fibonnaci sequence using
    ``%fib = recursive (%a:i32) > i32 > (select (gt %a #i1) (mul %a (apply %fib (sub %a #i1))) #i1)``
    should be valid. The compiler failed in this case because the ``select`` call will
    not be translated into a loop, but rather an infinite sequence of instructions.