Syntax
======

.. _psi.tvm.numerical_constants:

Grammar
-------

.. productionlist:: tvm
  global: ( ID "=" ( `global_variable` | `function` | `definition` ) ";" )*
  global_variable: "global" [ "const" ] `expression` [ `expression` ]
  definition: "define" `root_expression`
  function: `function_type` "{" `statement_list` `block_list` "}"
  root_expression: `NUMBER`
                 : `ID`
                 : `OP` ( `expression` )*
                 : `dependent`
                 : "(" `root_expression` ")"
  expression: `NUMBER`
            : `ID`
            : `OP`
            : "(" `root_expression` ")"
  dependent: "function" "(" `parameter_list` ")" ">" `expression`
           : "exists" "(" `parameter_list` ")" ">" `expression`
  

Idenifiers
----------

Identifiers are the user variable namespace. They are marked with ``%``, and for purposes of input
the rest of the name is restricted to :regexp:`[a-zA-Z0-9_]+`.

Numerical constants
-------------------

#up

