#include "Tree.hpp"

#include <boost/checked_delete.hpp>
#include <boost/bind.hpp>

namespace Psi {
  namespace Compiler {
    Tree::Tree(CompileContext& compile_context, const SourceLocation& location)
    : m_compile_context(&compile_context),
    m_location(location) {
    }
    
    void Tree::complete(bool dependency) {
      m_completion_state.complete(compile_context(), m_location, dependency,
                                  boost::bind(derived_vptr()->complete_callback, this));
    }

    Type::Type(CompileContext& compile_context, const SourceLocation& location)
    : Expression(compile_context, location) {
    }
    
    /**
     * \brief Rewrite a term, substituting new trees for existing ones.
     *
     * \param location Location to use for error reporting.
     * \param substitutions Substitutions to make.
     */
    TreePtr<Type> Type::rewrite(const SourceLocation& location, const Map<TreePtr<Type>, TreePtr<Type> >& substitutions) {
      if (!this)
        return TreePtr<Type>();

      Map<TreePtr<>, TreePtr<> >::const_iterator i = substitutions.find(TreePtr<Type>(this));
      if (i != substitutions.end())
        return i->second;
      else
        return TreePtr<Type>(derived_vptr()->rewrite(this, &location, &substitutions), false);
    }

    GlobalTree::GlobalTree(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    ExternalGlobalTree::ExternalGlobalTree(const TreePtr<Type>& type, const SourceLocation& location)
    : GlobalTree(type, location) {
    }

    FunctionType::FunctionType(CompileContext& context, const SourceLocation& location)
    : Type(context, location) {
    }

    FunctionType::~FunctionType() {
    }

    template<typename Visitor>
    void FunctionType::visit_impl(FunctionType& self, Visitor& visitor) {
      Type::visit_impl(self, visitor);
      visitor
      ("arguments", self.arguments)
      ("result_type", self.result_type);
    }

    TreePtr<> FunctionType::rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions) {
      PSI_FAIL("need to sort out function type equivalence checking");
      
      for (ArrayList<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<> rw_type = (*ii)->type()->rewrite(location, substitutions);
        if (rw_type != (*ii)->type())
          goto rewrite_required;
      }
      return TreePtr<>(this);

    rewrite_required:
      std::map<TreePtr<>, TreePtr<> > child_substitutions(substitutions);
      ArrayList<TreePtr<FunctionTypeArgument> > rw_arguments;
      for (ArrayList<TreePtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        TreePtr<> rw_type = (*ii)->type()->rewrite(location, child_substitutions);
        TreePtr<Type> rw_cast_type = dyn_treeptr_cast<Type>(rw_type);
        if (!rw_cast_type)
          compile_context().error_throw(location, "Rewritten function argument type is not a type");

        TreePtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(rw_cast_type, this->location()));
        rw_arguments.push_back(rw_arg);
        child_substitutions[*ii] = rw_arg;
      }

      TreePtr<> rw_result = result_type->type()->rewrite(location, child_substitutions);
      TreePtr<Type> cast_rw_result = dyn_treeptr_cast<Type>(rw_result);
      if (!cast_rw_result)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      TreePtr<FunctionType> rw_self(new FunctionType(compile_context(), this->location()));
      rw_self->arguments.swap(rw_arguments);

      return rw_self;
    }

    TreePtr<Type> FunctionType::argument_type_after(const SourceLocation& location, const ArrayList<TreePtr<> >& previous) {
      if (previous.size() >= arguments.size())
        compile_context().error_throw(location, "Too many arguments passed to function");
      
      std::map<TreePtr<>, TreePtr<> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = arguments[previous.size()]->type()->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dyn_treeptr_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return cast_type;
    }
    
    TreePtr<Type> FunctionType::result_type_after(const SourceLocation& location, const ArrayList<TreePtr<> >& previous) {
      if (previous.size() != arguments.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      std::map<TreePtr<>, TreePtr<> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      TreePtr<> type = result_type->rewrite(location, substitutions);
      TreePtr<Type> cast_type = dyn_treeptr_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return cast_type;
    }

    FunctionTypeArgument::FunctionTypeArgument(const TreePtr<Type>& type, const SourceLocation& location)
    : Value(type, location) {
    }

    FunctionTypeArgument::~FunctionTypeArgument() {
    }

    void FunctionTypeArgument::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
    }

    Function::Function(const TreePtr<FunctionType>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    Function::~Function() {
    }

    void Function::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor.visit_range(arguments);
      visitor % body;
    }

    FunctionArgument::FunctionArgument(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    FunctionArgument::~FunctionArgument() {
    }

    TryFinally::TryFinally(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    TryFinally::~TryFinally() {
    }

    void TryFinally::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % try_block % finally_block;
    }

    Block::Block(const TreePtr<Type>& type, const SourceLocation& location)
    : Tree(type, location) {
    }

    Block::~Block() {
    }

    void Block::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      for (ArrayList<TreePtr<Statement> >::iterator ii = statements.begin(), ie = statements.end(); ii != ie; ++ii)
        visitor % *ii;
    }

    Statement::Statement(const TreePtr<>& value_, const SourceLocation& location)
    : Tree(value_->type(), location), value(value_) {
    }

    Statement::~Statement() {
    }

    void Statement::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % value;
    }
  }
}