Object lifecycle
================

Object lifecycles are managed by 6 functions organised into two interfaces.
The functions are

Construct
  Default constructor for an object, takes no parameters other than a pointer to memory to construct the object in.

Destroy
  Release any resources held by the object.
  
Move
  Copy the value of one object into another, transferring the resources of the source object into the destination.
  The value of the source object is undefined after this operation (obviously it must still be valid because destroy will still be run on it).
  
MoveConstruct
  Construct an object, copy the value of another object and taking ownership of its resources.

Copy
  Copy the value of one object into another, cloning the resources of the source object as required.
  
CopyConstruct
  Create a new object, copying the value of an existing one and cloning resources as necessary.
  
These are organised into two interfaces, Movable and Copyable.
Almost all objects should implement Movable, whether Copyable is supported depends on the resources the object holds.
Their definitions are::

  MovableType : struct (u : upref, x : type) [
    init : function (self : derived(MovableType(u,x),u), dest :: pointer(x));
    fini : function (self : derived(MovableType(u,x),u), dest :: pointer(x));
    move : function (self : derived(MovableType(u,x),u), dest :: pointer(x), src :: pointer(x));
    move_init : function (self : derived(MovableType(u,x),u), dest :: pointer(x), src :: pointer(x));
  ];

  Movable : interface (x : type => exists (u:upref => derived(MovableType(u,x),u)));
  
  CopyableType : struct (u : upref, x : type) [
    copy : function (self : derived(MovableType(u,x),u), dest :: pointer(x), src :: pointer(x));
    copy_init : function (self : derived(MovableType(u,x),u), dest :: pointer(x), src :: pointer(x));
  ];
  
  Copyable : interface (x : type => exists (u:upref => derived(CopyableType(u,x),u))) (Movable(x));
