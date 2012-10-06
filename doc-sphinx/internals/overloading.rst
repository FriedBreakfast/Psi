Overloading
===========

The overload mechanism is used in two different cases:
first, for locating object metadata, which is a compile-time Tree value with a fixed static type,
and second for locating interfaces which are run-time values.

Overload types
--------------

Overload types specify the number and pattern of parameters used to select an overload value.
They may have any number of initial parameters whose value is inferred.

Overload values
---------------

Overload values specify an additional set of free parameters and a pattern to match.
The free parameters are inferred from the user-specified parameters if the pattern is successfully matched.

Resolution
----------

Overload resolution mechanism consists of three stages.

Interfaces
----------

Interface types have an additional component to their pattern.
They may specify an additional set of parameters upon which their type depends, but are not specified by pattern matching with the user-specified parameters,
rather they are fetched from the implementation once a particular implementation is selected.