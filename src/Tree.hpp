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

    /**
     * \brief A type for values containing no data.
     *
     * This is not \em the empty type exactly, since macros may give different behaviour
     * to distinct instances of empty type objects.
     */
    class EmptyType : public Type {
    public:
      EmptyType(CompileContext&);
      virtual ~EmptyType();
    };

    /**
     * \brief A value containing no data.
     *
     * This is not necessarily a value of \em the empty type, see EmptyType for details.
     */
    class EmptyValue : public Tree {
    public:
      EmptyValue(const GCPtr<EmptyType>&);
      virtual ~EmptyValue();
    };

    class UnaryOperation : public Tree {
    protected:
      UnaryOperation(CompileContext&);
      UnaryOperation(const UnaryOperation&);
      virtual void gc_visit(GCVisitor&);
      typedef UnaryOperation RewriteDuplicateType;
      virtual GCPtr<UnaryOperation> rewrite_duplicate_hook() = 0;

    public:
      virtual ~UnaryOperation();
      virtual GCPtr<Tree> rewrite_hook(const SourceLocation&, const std::map<GCPtr<Tree>, GCPtr<Tree> >&);

      GCPtr<Tree> child;
    };

    class BinaryOperation : public Tree {
    protected:
      BinaryOperation(CompileContext&);
      BinaryOperation(const BinaryOperation&);
      virtual void gc_visit(GCVisitor&);
      typedef BinaryOperation RewriteDuplicateType;
      virtual GCPtr<BinaryOperation> rewrite_duplicate_hook() = 0;

    public:
      virtual ~BinaryOperation();
      virtual GCPtr<Tree> rewrite_hook(const SourceLocation&, const std::map<GCPtr<Tree>, GCPtr<Tree> >&);

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
      virtual GCPtr<Tree> rewrite_hook(const SourceLocation&, const std::map<GCPtr<Tree>, GCPtr<Tree> >&);

      GCPtr<Type> argument_type_after(const SourceLocation&, const std::vector<GCPtr<Tree> >&);
      GCPtr<Type> result_type_after(const SourceLocation&, const std::vector<GCPtr<Tree> >&);

      std::vector<GCPtr<FunctionTypeArgument> > arguments;
      GCPtr<Type> result_type;
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
      GCPtr<Tree> result_type;
      GCPtr<Tree> body;
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
      virtual GCPtr<base::RewriteDuplicateType> rewrite_duplicate_hook(); \
    public: \
      name(CompileContext&); \
      virtual ~name(); \
    };

#include "TreeOperations.def"
#undef PSI_TREE_OPERATION
  }
}

#endif
