TVM type mapping
================

This document covers how various types are mapped to :ref:`psi.tvm`.

MoveConstructible
-----------------
::

  %move_constructible = recursive (%d : type, %m : member %d (apply %move_constructible %d)) > struct
    
  ;
