Issues
======

Error message merging
---------------------

Currently, TVM reports errors entirely by throwing exceptions.
I need to create an error reporting mechanism which is shared by TVM and the high-level code,
so that any errors occuring in the back-end will provide useful feedback to the user.

Visitor versus overloading
--------------------------

.. highlight:: c++

The macro based system I'm currently using to generate implementations of virtual functions
through the ``visit()`` mechanism does not easily allow overloading (note the ``match_visit`` flag).
I should replace it with the following approach::

  class Base {
  protected:
    template<typename T>
    void callback_impl(VisitorTag<T>) {
      MyVisitor<T> visitor(static_cast<T*>(this));
      T::visit(visitor);
    }
  
  public:
    virtual void callback() = 0;
  };

  #define CLASS_IMPL(name,base) \
    virtual void callback() {name::callback_impl(VisitorTag<name>());}
    
The ``callback_impl`` can be overloaded easily.

Type compatibility comparison
-----------------------------

Even with term merging and hashing, the fact remains that what we really want to know
is whether a value of type ``A`` can also be said to have type ``B``.
Some cases remain where this is not the same as whether ``A=B``.
Namely:

* uprefs with a non-NULL next pointer can substitute for one with a NULL next pointer.

Term rewriting
--------------

Specialize, parameterize and anonymize all need to be re-checked.

Faster term lowering
--------------------

I really need to remove all of these constructions::

  if (TreePtr<X> = dyn_treeptr_cast<X>(y)) ...
  
It should be replaced with a clone of :psi:`Tvm::TermOperationMap`.

Simplifying dynamic tree evaluation
-----------------------------------

Dynmic evalution of trees seems to only really be necessary in a few cases:

* Generic types
* Overload values attached to overload types
* Global variable values
* Function bodies

It might also be convenient to support dynamic evaluation for overload value bodies generally.
Note that the mechanism for lazy evaluation should not be restricted to these types -
lazy evaluation is still required for generating :psi:`EvaluateContext` instances from blocks
and namespaces because the context must be created before the elements are evaluated.

I should therefore be able to remove :psi:`TreeBase` entirely in favour of adding a little functionality to the relevant classes.

Term hashing and substitutability
---------------------------------

I need to bring term hashing and type substitutability testing to the high level code.
Preferably this should include merging equivalent terms, so I must remove use of the ``new`` operator,
which will take some work, but hopefully save some typing in the end.

The high level data structures are rather different to TVM, so some differences will occur:

* A tree which contains stateful evaluation should be identified as such and not partake in
  term hashing because it is never equivalent to another tree (even itself if evaluated again).
* The result mode of each tree should be tracked, i.e. lvalue/rvalue ref, by value or functional.
  This includes ``BottomType``, which has a special mode.
  
This entails some further things:

1. Type checking should be performed upon construction of terms.
   This will vastly improve the compiler's error reporting capability.
   For types with internal delayed callbacks as described above, any checks
   shall be performed when the callback is run.
2. TVM lowering can now store a pre-existing terms hash.
3. Some constant folding can be performed at this level, so I should put in
   place a mechanism to enable this.
   I do not plan to use this currently though, because I'm not sure a program
   should require constant folding as part of its type check.

The bottom type
---------------

At some point I need to define an interface which details how to merge differing return values of
two trees to create a user-friendly if-then-else statement.
In case one or both of those types is the bottom type, the interface will expect a function that
takes a value of ``BottomType`` and return some other type.
Obviously no such function can be written, but this can be circumvented by a rule:
any function expecting a parameter of type ``BottomType`` should not have a body,
since it can never be called because the parameter cannot itself be constructed.

It is forbidden to create the tree ``DefaultValue(BottomType())``.
The high level tree representation does not currently have an undefined value tree, and I do not
see a reason this will change, but if it does, then an undefined value of bottom type should also
be forbidden.

Functional/stateful term split
------------------------------

The whole term hierarchy should probably be subdivided according to functional vs. non-functional terms.

Consider making Tvm::Generic a type in its own right
----------------------------------------------------

This would obviate the need to ``dyn_unrecurse`` and has no disadvantages if I'm not providing true functional programming.
It will also match up with the behaviour of the high level tree language.
I will need to provide ``unrecurse`` and ``apply_v`` operations and extend ``element_ptr`` and ``element_value`` accordingly.

Closures
--------

Need to scan for variables used so I can generate the internal closure variable type.
Closures simply don't exist at the moment.
I also need to allow local interface implementations, which will interact with closures.

Syntax
------

Constructor expressions
"""""""""""""""""""""""

When assigning to something whose type is known, use the known type information to
allow the user to just write the necessary values without having to give the type
information again.

Could be useful for struct, union or function pointer types.

Smarter block compilation
"""""""""""""""""""""""""

Make block compilation semi-smart, so obviously functional statements (typedefs) are automatically detected.
I'm not entirely sure statement_mode_functional should really exist (or rather, be used as much as it is), and what the hell is GlobalDefine for?

C++ like object lifetimes
-------------------------

Do not destroy temporaries until explicit lifetime qualifier is encountered; i.e. the end of the current statement.

Specialization and parameterization
-----------------------------------

I'm not at all sure that the current implementation works properly.
It's almost certainly buggy because of the complexity.
I think this whole system needs a rethink.

TVM improvements
----------------

* Constant folding should not be performed by the FunctionalBuilder class.
  Instead it should be performed by each class itself, which will mean:
  
  * Type checking can be run before constant folding in debug mode without
    causing problems in the code (this choice will be made in only one place),
    so no slip ups in term generation will get through this way.
    
  * It is not possible to construct a term in non-canoncal form, although
    I believe I'm using the FunctionalBuilder class everywhere at the moment
    so this should not be happening.
  
* Several cases of constant folding are missing, particularly bit-wise operations.

* Function parameter attributes, particularly ``nonnull``, which should be set for
  reference-type arguments.
  
* Add some sort of hardware exception mechanism: currently division-by-zero and NULL
  pointer errors will cause program termination.
  It would be better to provide some options to the user to let this be handled gracefully
  most of the time, or to remove error checking when performance is needed (locally, not
  globally though).
