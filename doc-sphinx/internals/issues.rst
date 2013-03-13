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

Constant folding
----------------

I should put in a mechanism to enable functional terms to be optimized where possible.

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
