Glossary
========

.. glossary::
  :sorted:

Phantom value
  *Phantom* values implement the mathematical notion of :math:`\exists x. f(x)`,
  playing the role of :math:`x` here. A phantom value cannot be used for any operation
  where its value can influence memory access; that is load, store and function calls
  (unless the function expects a phantom value as a parameter).