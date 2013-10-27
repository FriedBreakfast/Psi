#ifndef HPP_PSI_INTERFACE
#define HPP_PSI_INTERFACE

#include "Compiler.hpp"
#include "Tree.hpp"
#include "Parser.hpp"

/**
 * \file
 * 
 * Place for interfaces which have no better place to go.
 */

namespace Psi {
namespace Compiler {
struct ImplementationSetup {
  /// \brief Interface being implemented
  TreePtr<Interface> interface;
  /// \brief Parameters which describe the pattern of the implementation
  PSI_STD::vector<TreePtr<Anonymous> > pattern_parameters;
  /// \brief Additional interfaces required by the implementation
  PSI_STD::vector<TreePtr<InterfaceValue> > pattern_interfaces;
  /// \brief Pattern of interface parameters being implemented, in terms of \c pattern_parameters
  PSI_STD::vector<TreePtr<Term> > interface_parameters;
  
  ImplementationSetup() {}
  
  ImplementationSetup(const TreePtr<Interface>& interface_,
                      const PSI_STD::vector<TreePtr<Anonymous> >& pattern_parameters_,
                      const PSI_STD::vector<TreePtr<InterfaceValue> >& pattern_interfaces_,
                      const PSI_STD::vector<TreePtr<Term> >& interface_parameters_)
  : interface(interface_),
  pattern_parameters(pattern_parameters_),
  pattern_interfaces(pattern_interfaces_),
  interface_parameters(interface_parameters_) {}
  
  template<typename V>
  static void visit(V& v) {
    v("interface", &ImplementationSetup::interface)
    ("pattern_parameters", &ImplementationSetup::pattern_parameters)
    ("pattern_interfaces", &ImplementationSetup::pattern_interfaces)
    ("interface_parameters", &ImplementationSetup::interface_parameters);
  }
};

struct ImplementationMemberSetup {
  ImplementationSetup base;
  TreePtr<Term> type;
  
  template<typename V>
  static void visit(V& v) {
    v("base", &ImplementationMemberSetup::base)
    ("type", &ImplementationMemberSetup::type);
  }
};

class InterfaceMemberCallback;

struct InterfaceMemberCallbackVtable {
  TreeVtable base;
  void (*evaluate) (TreePtr<Term> *result, const InterfaceMemberCallback *self, const TreePtr<Interface> *interface, const PSI_STD::vector<unsigned> *path, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location);
  void (*implement) (TreePtr<Term> *result, const InterfaceMemberCallback *self, const ImplementationMemberSetup *setup, const SharedPtr<Parser::Expression> *value, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location);
};

/**
 * Callbacks for interface members.
 */
class InterfaceMemberCallback : public Tree {
public:
  typedef InterfaceMemberCallbackVtable VtableType;
  
  InterfaceMemberCallback(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
  : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {}
  
  TreePtr<Term> evaluate(const TreePtr<Interface>& interface,
                         const PSI_STD::vector<unsigned>& path,
                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                         const TreePtr<EvaluateContext>& evaluate_context,
                         const SourceLocation& location) const {
    ResultStorage<TreePtr<Term> > rs;
    derived_vptr(this)->evaluate(rs.ptr(), this, &interface, &path, &parameters, &evaluate_context, &location);
    return rs.done();
  }
  
  TreePtr<Term> implement(const ImplementationMemberSetup& setup,
                          const SharedPtr<Parser::Expression>& value,
                          const TreePtr<EvaluateContext>& evaluate_context,
                          const SourceLocation& location) const {
    ResultStorage<TreePtr<Term> > rs;
    derived_vptr(this)->implement(rs.ptr(), this, &setup, &value, &evaluate_context, &location);
    return rs.done();
  }
};

template<typename Derived, typename Impl=Derived>
struct InterfaceMemberCallbackWrapper {
  static void evaluate(TreePtr<Term> *result, const InterfaceMemberCallback *self, const TreePtr<Interface> *interface, const PSI_STD::vector<unsigned> *path, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
    new (result) TreePtr<Term> (Impl::evaluate_impl(*static_cast<const Derived*>(self), *interface, *path, *parameters, *evaluate_context, *location));
  }
  
  static void implement(TreePtr<Term> *result, const InterfaceMemberCallback *self, const ImplementationMemberSetup *setup, const SharedPtr<Parser::Expression> *value, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
    new (result) TreePtr<Term> (Impl::implement_impl(*static_cast<const Derived*>(self), *setup, *value, *evaluate_context, *location));
  }
};

#define PSI_COMPILER_INTERFACE_MEMBER_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::InterfaceMemberCallbackWrapper<derived>::evaluate, \
    &::Psi::Compiler::InterfaceMemberCallbackWrapper<derived>::implement, \
  }

/**
 * \brief Type passed to interface member construction.
 */
struct InterfaceMemberArgument {
  /// \brief Generic type that the interface will generate
  TreePtr<GenericType> generic;
  /// \brief Interface type parameters
  PSI_STD::vector<TreePtr<Term> > parameters;
  /// \brief Pointer type for internal references to the interface; should be used as a function parameter
  TreePtr<Term> self_pointer_type;
};

/**
 * \brief Result of interface member construction.
 */
struct InterfaceMemberResult {
  /// \brief Member type
  TreePtr<Term> type;
  /// \brief Callback used to implement and evaluate this member
  TreePtr<InterfaceMemberCallback> callback;
};

struct PatternArguments {
  PSI_STD::vector<TreePtr<Anonymous> > list;
  PSI_STD::vector<TreePtr<Anonymous> > dependent;
  PSI_STD::map<String, TreePtr<Term> > names;
  
  template<typename V>
  static void visit(V& v) {
    v("list", &PatternArguments::list)
    ("names", &PatternArguments::names);
  }
};

PatternArguments parse_pattern_arguments(const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location, const Parser::Text& text);
PSI_STD::vector<TreePtr<Term> > arguments_to_pattern(const PSI_STD::vector<TreePtr<Anonymous> >& arguments, const PSI_STD::vector<TreePtr<Anonymous> >& =PSI_STD::vector<TreePtr<Anonymous> >());
TreePtr<Term> interface_member_pattern(const TreePtr<Interface>& interface, const PSI_STD::vector<unsigned>& path, const SourceLocation& location);
}
}

#endif
