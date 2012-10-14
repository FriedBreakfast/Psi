Overloading
===========

The overload mechanism is used in two different cases:
first, for locating object metadata, which is a compile-time Tree value with a fixed static type,
and second for locating interfaces which are run-time values.

Overload types
--------------

Overload types specify the number and pattern of parameters used to select an overload value.
An overload type pattern is like a function parameter list, hence each entry pattern is in fact a type.
As such they may have a number of initial parameters (usually dependent types) whose value is inferred by pattern matching on the remaining parameters.

Overload values
---------------

Overload values are possible values an overload type can take on depending on the parameters.
They specify a pattern which must match the user's parameters in order to apply.
This pattern will contain a set of wildcards which are inferred from the user-specified parameters.
However, in contrast to functions, the patterns match the user-specified values rather than their types.
Therefore, only the wildcards are actually considered parameters to the overloaded value since everything else is fixed.

Resolution
----------

Overload resolution mechanism consists of three stages.

Interfaces
----------

Interface types have an additional component to their pattern.
They may specify an additional set of parameters upon which their type depends, but are not specified by pattern matching with the user-specified parameters,
rather they are fetched from the implementation once a particular implementation is selected.