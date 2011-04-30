#include "Tree.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Compiler {
    Tree::Tree(CompileContext& compile_context) : CompileObject(compile_context) {
    }

    Tree::~Tree() {
    }

    void Tree::gc_visit(GCVisitor& visitor) {
      visitor % dependency % type;
    }

    GCPtr<Tree> Tree::rewrite_hook(const SourceLocation&, const std::map<GCPtr<Tree>, GCPtr<Tree> >&) {
      return GCPtr<Tree>(this);
    }

    /**
      * \brief Rewrite a term, substituting new trees for existing ones.
      *
      * \param location Location to use for error reporting.
      * \param substitutions Substitutions to make.
      */
    GCPtr<Tree> Tree::rewrite(const SourceLocation& location, const std::map<GCPtr<Tree>, GCPtr<Tree> >& substitutions) {
      if (!this)
        return GCPtr<Tree>();

      GCPtr<Tree> this_gc(this);
      std::map<GCPtr<Tree>, GCPtr<Tree> >::const_iterator i = substitutions.find(this_gc);
      if (i != substitutions.end())
        return i->second;
      else
        return rewrite_hook(location, substitutions);
    }

    Type::Type(CompileContext& context) : Tree(context) {
    }

    Type::~Type() {
    }

    void Type::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % type;
    }

    EmptyType::EmptyType(CompileContext& compile_context) : Type(compile_context) {
    }

    EmptyType::~EmptyType() {
    }

    EmptyValue::EmptyValue(const GCPtr<EmptyType>& type) : Tree(type->compile_context()) {
      this->type = type;
    }

    EmptyValue::~EmptyValue() {
    }

    UnaryOperation::UnaryOperation(CompileContext& context) : Tree(context) {
    }

    UnaryOperation::UnaryOperation(const UnaryOperation& src)
    : Tree(src.compile_context()), child(src.child) {
      type = src.type;
    }

    UnaryOperation::~UnaryOperation() {
    }

    void UnaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % child;
    }

    GCPtr<Tree> UnaryOperation::rewrite_hook(const SourceLocation& location, const std::map<GCPtr<Tree>, GCPtr<Tree> >& substitutions) {
      GCPtr<Tree> rw_child = child->rewrite(location, substitutions);
      if (rw_child == child)
        return this;

      GCPtr<UnaryOperation> rw_self = rewrite_duplicate_hook();
      rw_self->child = rw_child;
      return rw_self;
    }

    BinaryOperation::BinaryOperation(CompileContext& context) : Tree(context) {
    }

    BinaryOperation::BinaryOperation(const BinaryOperation& src)
    : Tree(src.compile_context()), left(src.left), right(src.right) {
      type = src.type;
    }

    BinaryOperation::~BinaryOperation() {
    }

    void BinaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % left % right;
    }

    GCPtr<Tree> BinaryOperation::rewrite_hook(const SourceLocation& location, const std::map<GCPtr<Tree>, GCPtr<Tree> >& substitutions) {
      GCPtr<Tree> rw_left = left->rewrite(location, substitutions);
      GCPtr<Tree> rw_right = right->rewrite(location, substitutions);
      if ((left != rw_left) || (right != rw_right))
        return this;

      GCPtr<BinaryOperation> rw_self = rewrite_duplicate_hook();
      rw_self->left = rw_left;
      rw_self->right = rw_right;
      return rw_self;
    }

    FunctionType::FunctionType(CompileContext& context) : Type(context) {
    }

    FunctionType::~FunctionType() {
    }

    void FunctionType::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % result_type;
      visitor.visit_range(arguments);
    }

    GCPtr<Tree> FunctionType::rewrite_hook(const SourceLocation& location, const std::map<GCPtr<Tree>, GCPtr<Tree> >& substitutions) {
      PSI_FAIL("need to sort out function type equivalence checking");
      
      for (std::vector<GCPtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        GCPtr<Tree> rw_type = (*ii)->type->rewrite(location, substitutions);
        if (rw_type != (*ii)->type)
          goto rewrite_required;
      }
      return GCPtr<Tree>(this);

    rewrite_required:
      GCPtr<FunctionType> rw_self(new FunctionType(compile_context()));
      rw_self->arguments.reserve(arguments.size());

      std::map<GCPtr<Tree>, GCPtr<Tree> > child_substitutions(substitutions);
      std::vector<GCPtr<FunctionTypeArgument> > rw_arguments;
      for (std::vector<GCPtr<FunctionTypeArgument> >::iterator ii = arguments.begin(), ie = arguments.end(); ii != ie; ++ii) {
        GCPtr<Tree> rw_type = (*ii)->type->rewrite(location, child_substitutions);
        GCPtr<Type> rw_cast_type = dynamic_pointer_cast<Type>(rw_type);
        if (!rw_cast_type)
          compile_context().error_throw(location, "Rewritten function argument type is not a type");

        GCPtr<FunctionTypeArgument> rw_arg(new FunctionTypeArgument(compile_context()));
        rw_arg->type = rw_cast_type;
        rw_self->arguments.push_back(rw_arg);
        child_substitutions[*ii] = rw_arg;
      }

      GCPtr<Tree> rw_result = result_type->type->rewrite(location, child_substitutions);
      rw_self->result_type = dynamic_pointer_cast<Type>(rw_result);
      if (!rw_self->result_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return rw_self;
    }

    GCPtr<Type> FunctionType::argument_type_after(const SourceLocation& location, const std::vector<GCPtr<Tree> >& previous) {
      if (previous.size() >= arguments.size())
        compile_context().error_throw(location, "Too many arguments passed to function");
      
      std::map<GCPtr<Tree>, GCPtr<Tree> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      GCPtr<Tree> type = arguments[previous.size()]->type->rewrite(location, substitutions);
      GCPtr<Type> cast_type = dynamic_pointer_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function argument type is not a type");

      return cast_type;
    }
    
    GCPtr<Type> FunctionType::result_type_after(const SourceLocation& location, const std::vector<GCPtr<Tree> >& previous) {
      if (previous.size() != arguments.size())
        compile_context().error_throw(location, "Incorrect number of arguments passed to function");

      std::map<GCPtr<Tree>, GCPtr<Tree> > substitutions;
      for (unsigned ii = 0, ie = previous.size(); ii != ie; ++ii)
        substitutions[arguments[ii]] = previous[ii];

      GCPtr<Tree> type = result_type->rewrite(location, substitutions);
      GCPtr<Type> cast_type = dynamic_pointer_cast<Type>(type);
      if (!cast_type)
        compile_context().error_throw(location, "Rewritten function result type is not a type");

      return cast_type;
    }

    FunctionTypeArgument::FunctionTypeArgument(CompileContext& context) : Tree(context) {
    }

    FunctionTypeArgument::~FunctionTypeArgument() {
    }

    void FunctionTypeArgument::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
    }

    Function::Function(CompileContext& context) : Tree(context) {
    }

    Function::~Function() {
    }

    void Function::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor.visit_range(arguments);
      visitor % body;
    }

    FunctionArgument::FunctionArgument(CompileContext& context) : Tree(context) {
    }

    FunctionArgument::~FunctionArgument() {
    }

    void FunctionArgument::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % type;
    }

    TryFinally::TryFinally(CompileContext& context) : Tree(context) {
    }

    TryFinally::~TryFinally() {
    }

    void TryFinally::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % try_block % finally_block;
    }

    Block::Block(CompileContext& context) : Tree(context) {
    }

    Block::~Block() {
    }

    void Block::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % statements;
    }

    Statement::Statement(CompileContext& context) : Tree(context) {
    }

    Statement::~Statement() {
    }

    void Statement::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % next % value;
    }

#define PSI_TREE_OPERATION(name,base) \
    name::name(CompileContext& context) : base(context) { \
    } \
    \
    name::~name() { \
    } \
    \
    GCPtr<base::RewriteDuplicateType> name::rewrite_duplicate_hook() { \
      return new name(*this); \
    }

#include "TreeOperations.def"
#undef PSI_TREE_OPERATION
  }
}