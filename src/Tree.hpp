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
      StructType(CompileContext&);
      virtual ~StructType();
    };

    class UnionType : public Type {
    public:
      UnionType(CompileContext&);
      virtual ~UnionType();
    };

    class UnaryOperation : public Tree {
    protected:
      UnaryOperation(CompileContext&);
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~UnaryOperation();

      GCPtr<Tree> child;
    };

    class BinaryOperation : public Tree {
    protected:
      BinaryOperation(CompileContext&);
      virtual void gc_visit(GCVisitor&);

    public:
      virtual ~BinaryOperation();

      GCPtr<Tree> left, right;
    };

    class FunctionTypeArgument : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      FunctionTypeArgument(CompileContext&);
      virtual ~FunctionTypeArgument();
    };

    class FunctionType : public Type {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      FunctionType(CompileContext&);
      virtual ~FunctionType();

      std::vector<GCPtr<FunctionTypeArgument> > arguments;
    };

    class FunctionArgument : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      FunctionArgument(CompileContext&);
      virtual ~FunctionArgument();
    };

    class Function : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Function(CompileContext&);
      virtual ~Function();

      std::vector<GCPtr<FunctionArgument> > arguments;
      GCPtr<Tree>  body;
    };

    class TryFinally : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      TryFinally(CompileContext&);
      virtual ~TryFinally();

      GCPtr<Tree> try_block, finally_block;
    };

#define PSI_TREE_OPERATION(name,base) \
    class name : public base { \
    public: \
      name(CompileContext&); \
      virtual ~name(); \
    };

#include "TreeOperations.def"
#undef PSI_TREE_OPERATION
  }
}

#endif
