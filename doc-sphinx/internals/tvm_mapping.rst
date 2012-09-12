TVM type mapping
================

This document covers how various types are mapped to :ref:`psi.tvm`.

MoveConstructible
-----------------
::

  %move_constructible = recursive (%tag : upref_type, %target : type) > (struct
    (pointer (function (pointer (apply %move_constructible %tag %target) %tag, pointer %target) > empty))
    (pointer (function (pointer (apply %move_constructible %tag %target) %tag, pointer %target) > empty))
    (pointer (function (pointer (apply %move_constructible %tag %target) %tag, pointer %target, pointer %source) > empty))
  );

The order of functions is default constructor, destructor, move constructor.

CopyConstructible
-----------------
::

  %copy_constructible = recursive (%tag : upref, %target : type) > (struct
    (apply %move_constructible (upref (apply %copy_constructible %tag %target) #up0 %tag) %target)
    (pointer (function (pointer (apply %copy_constructible %tag %target) %tag, pointer %target, pointer %source) > empty))
    (pointer (function (pointer (apply %copy_constructible %tag %target) %tag, pointer %target, pointer %source) > empty))
  );
