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
class InterfaceMemberCallback;

struct InterfaceMemberCallbackVtable {
  TreeVtable base;
  void (*evaluate) (TreePtr<Term> *result, const InterfaceMemberCallback *self, const PSI_STD::vector<unsigned> *path, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location);
  void (*implement) (TreePtr<Term> *result, const InterfaceMemberCallback *self, const SharedPtr<Parser::Expression> *value, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location);
};

/**
 * Callbacks for interface members.
 */
class InterfaceMemberCallback : public Tree {
public:
  typedef InterfaceMemberCallbackVtable VtableType;
  
  InterfaceMemberCallback(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location)
  : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {}
  
  TreePtr<Term> evaluate(const PSI_STD::vector<unsigned>& path,
                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                         const TreePtr<EvaluateContext>& evaluate_context,
                         const SourceLocation& location) const {
    ResultStorage<TreePtr<Term> > rs;
    derived_vptr(this)->evaluate(rs.ptr(), this, &path, &parameters, &evaluate_context, &location);
    return rs.done();
  }
  
  TreePtr<Term> implement(const SharedPtr<Parser::Expression>& value,
                          const TreePtr<EvaluateContext>& evaluate_context,
                          const SourceLocation& location) const {
    ResultStorage<TreePtr<Term> > rs;
    derived_vptr(this)->implement(rs.ptr(), this, &value, &evaluate_context, &location);
    return rs.done();
  }
};

template<typename Derived, typename Impl=Derived>
struct InterfaceMemberCallbackWrapper {
  static void evaluate(TreePtr<Term> *result, const InterfaceMemberCallback *self, const PSI_STD::vector<unsigned> *path, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
    new (result) TreePtr<Term> (Impl::evaluate_impl(*static_cast<const Derived*>(self), *path, *parameters, *evaluate_context, *location));
  }
  
  static void implement(TreePtr<Term> *result, const InterfaceMemberCallback *self, const SharedPtr<Parser::Expression> *value, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
    new (result) TreePtr<Term> (Impl::implement_impl(*static_cast<const Derived*>(self), *value, *evaluate_context, *location));
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
}
}

#endif
