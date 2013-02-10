#ifndef HPP_PSI_INTERFACE
#define HPP_PSI_INTERFACE

#include "Compiler.hpp"
#include "Tree.hpp"

/**
 * \file
 * 
 * Place for interfaces which have no better place to go.
 */

namespace Psi {
namespace Compiler {
/**
 * \brief Holds basic constructor and destructor information for a type.
 */
struct TypeConstructorInfo {
  /**
   * \brief Whether this type is primitive.
   * 
   * If this is true, the default constructor does nothing, the move and copy
   * constructors are equivalent to bitwise copies and the destructor is a
   * no-op.
   */
  bool primitive;
  /// \brief Destructor
  TreePtr<Term> destructor;
  /// \brief Default constructor
  TreePtr<Term> default_constructor;
  /// \brief Move constructor
  TreePtr<Term> move_constructor;
  /// \brief Copy constructor
  TreePtr<Term> copy_constructor;
  /// \brief Parameter passed to all of these functions.
  TreePtr<Term> parameter;
  
  template<typename V>
  static void visit(V& v) {
    v("primitive", &TypeConstructorInfo::primitive)
    ("destructor", &TypeConstructorInfo::destructor)
    ("default_constructor", &TypeConstructorInfo::default_constructor)
    ("move_constructor", &TypeConstructorInfo::move_constructor)
    ("copy_constructor", &TypeConstructorInfo::copy_constructor)
    ("parameter", &TypeConstructorInfo::parameter);
  }
};

class TypeConstructorInfoCallback;

struct TypeConstructorInfoCallbackVtable {
  TreeVtable base;
  void (*type_constructor_info) (TypeConstructorInfo*, const TypeConstructorInfoCallback*);
};

class TypeConstructorInfoCallback : public Tree {
public:
  typedef TypeConstructorInfoCallbackVtable VtableType;
  static const SIVtable vtable;
  
  TypeConstructorInfo type_constructor_info() const {
    ResultStorage<TypeConstructorInfo> result;
    derived_vptr(this)->type_constructor_info(result.ptr(), this);
    return result.done();
  }
};

/**
 * \brief Indices of members in the Movable interface
 */
enum InterfaceMovableMembers {
  interface_movable_init=0,
  interface_movable_fini=1,
  interface_movable_move_init=2,
  interface_movable_move=3
};

/**
 * \brief Indices of members in the Copyable interface
 */
enum InterfaceCopyableMembers {
  interface_copyable_copy_init=0,
  interface_copyable_copy=1
};

TreePtr<Term> interface_type(const TreePtr<GenericType>& generic, const PSI_STD::vector<TreePtr<Term> >& introduced_parameters,
                              const PSI_STD::vector<TreePtr<Term> >& generic_parameters);
}
}

#endif
