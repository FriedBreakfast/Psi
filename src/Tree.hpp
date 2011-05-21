#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "GarbageCollection.hpp"
#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    class GlobalTree : public Tree {
      friend class CompileContext;
      void *m_jit_ptr;
    public:
      GlobalTree(const TreePtr<Type>&, const SourceLocation&);
      GlobalTree(const TreePtr<Type>&, const SourceLocation&, DependencyPtr&);
    };
    
    struct ExternalGlobalTree : GlobalTree {
      ExternalGlobalTree(const TreePtr<Type>&, const SourceLocation&);
      ExternalGlobalTree(const TreePtr<Type>&, const SourceLocation&, DependencyPtr&);
      String symbol_name;
    };
    
    /**
     * \brief Tree for a statement, which should be part of a block.
     */
    class Statement : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Statement(const TreePtr<>&, const SourceLocation&);
      virtual ~Statement();

      TreePtr<> value;
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Block(const TreePtr<Type>&, const SourceLocation&);
      Block(const TreePtr<Type>&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      virtual ~Block();

      ArrayList<TreePtr<Statement> > statements;
    };

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

    class FunctionTypeArgument : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      FunctionTypeArgument(const TreePtr<Type>&, const SourceLocation&);
      virtual ~FunctionTypeArgument();
    };

    class FunctionType : public Type {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      FunctionType(CompileContext&, const SourceLocation&);
      FunctionType(CompileContext&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      virtual ~FunctionType();
      virtual TreePtr<> rewrite_hook(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);

      TreePtr<Type> argument_type_after(const SourceLocation&, const ArrayList<TreePtr<> >&);
      TreePtr<Type> result_type_after(const SourceLocation&, const ArrayList<TreePtr<> >&);

      ArrayList<TreePtr<FunctionTypeArgument> > arguments;
      TreePtr<Type> result_type;
    };

    class FunctionArgument : public Tree {
    public:
      FunctionArgument(const TreePtr<Type>&, const SourceLocation&);
      virtual ~FunctionArgument();
    };

    class Function : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Function(const TreePtr<FunctionType>&, const SourceLocation&);
      Function(const TreePtr<FunctionType>&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      virtual ~Function();

      ArrayList<TreePtr<FunctionArgument> > arguments;
      TreePtr<> result_type;
      TreePtr<> body;
    };

    class TryFinally : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      TryFinally(const TreePtr<Type>&, const SourceLocation&);
      TryFinally(const TreePtr<Type>&, const SourceLocation&, typename MoveRef<DependencyPtr>::type);
      virtual ~TryFinally();

      TreePtr<> try_block, finally_block;
    };
  }
}

#endif
