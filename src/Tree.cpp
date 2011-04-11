#include "Tree.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  namespace Compiler {
    Tree::~Tree() {
    }

    void Tree::gc_visit(GCVisitor&) {
    }

    void Tree::gc_destroy() {
      delete this;
    }

    UnaryOperation::~UnaryOperation() {
    }

    void UnaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % child;
    }

    BinaryOperation::~BinaryOperation() {
    }

    void BinaryOperation::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % left % right;
    }

    TryFinally::~TryFinally() {
    }

    void TryFinally::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % try_block % finally_block;
    }

    Block::~Block() {
    }

    void Block::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % statements;
    }

    Statement::~Statement() {
    }

    void Statement::gc_visit(GCVisitor& visitor) {
      Tree::gc_visit(visitor);
      visitor % next % value;
    }

#define PSI_TREE_OPERATION(name,base) \
    name::name() { \
    } \
    \
    name::~name() { \
    }

#include "TreeOperations.def"
#undef PSI_TREE_OPERATION
  }
}