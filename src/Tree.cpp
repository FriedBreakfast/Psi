#include "Tree.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Compiler {
    Tree::Tree(CompileContext& context) {
      context.m_gc_pool.add(this);
    }

    Tree::~Tree() {
    }

    void Tree::gc_visit(GCVisitor& visitor) {
      visitor % dependency % type;
    }

    void Tree::gc_destroy() {
      delete this;
    }

    Type::Type(CompileContext& context) : Tree(context) {
    }

    Type::~Type() {
    }

    UnaryOperation::UnaryOperation(CompileContext& context) : Tree(context) {
    }

    UnaryOperation::~UnaryOperation() {
    }

    void UnaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % child;
    }

    BinaryOperation::BinaryOperation(CompileContext& context) : Tree(context) {
    }

    BinaryOperation::~BinaryOperation() {
    }

    void BinaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % left % right;
    }

    FunctionType::FunctionType(CompileContext& context) : Type(context) {
    }

    FunctionType::~FunctionType() {
    }

    void FunctionType::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor.visit_range(arguments);
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
    }

#include "TreeOperations.def"
#undef PSI_TREE_OPERATION
  }
}