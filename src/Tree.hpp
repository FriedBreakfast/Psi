#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Compiler {
    class StructType : public Type {
    public:
      virtual ~StructType();
    };

    class UnionType : public Type {
    public:
      virtual ~UnionType();
    };

    class UnaryOperation : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~UnaryOperation();

      TreePtr<> child;
    };

    class BinaryOperation : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~BinaryOperation();

      TreePtr<> left, right;
    };

    class TryFinally : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~TryFinally();

      TreePtr<> try_block, finally_block;
    };

#define PSI_TREE_OPERATION(name,base) \
    class name : public base { \
      friend class Context; \
      name(); \
    protected: \
      virtual ~name(); \
    };

#include "TreeOperations.def"
#undef PSI_TREE_OPERATION
  }
}

#endif
