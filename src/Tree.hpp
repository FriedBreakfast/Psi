#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    class GlobalTree : public Tree {
      friend class CompileContext;
      void *m_jit_ptr;
    public:
      GlobalTree(const TreePtr<Type>&, const SourceLocation&);

      template<typename Visitor> static void visit_impl(GlobalTree&, Visitor&);
    };
    
    struct ExternalGlobalTree : GlobalTree {
      ExternalGlobalTree(const TreePtr<Type>&, const SourceLocation&);
      String symbol_name;
    };
    
    /**
     * \brief Tree for a statement, which should be part of a block.
     */
    class Statement : public Expression {
    public:
      Statement(CompileContext&, const SourceLocation&);
      virtual ~Statement();

      TreePtr<> value;

      template<typename Visitor> static void visit_impl(Statement&, Visitor&);
      void complete_statement();
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Tree {
    public:
      Block(const TreePtr<Type>&, const SourceLocation&);
      virtual ~Block();

      PSI_STD::vector<TreePtr<Statement> > statements;

      template<typename Visitor> static void visit_impl(Block&, Visitor&);
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

    class FunctionTypeArgument : public Value {
    public:
      FunctionTypeArgument(const TreePtr<Type>&, const SourceLocation&);
      virtual ~FunctionTypeArgument();
    };

    class FunctionType : public Type {
    public:
      FunctionType(CompileContext&, const SourceLocation&);
      virtual ~FunctionType();
      virtual TreePtr<> rewrite_hook(const SourceLocation&, const Map<TreePtr<>, TreePtr<> >&);

      TreePtr<Type> argument_type_after(const SourceLocation&, const List<TreePtr<> >&);
      TreePtr<Type> result_type_after(const SourceLocation&, const List<TreePtr<> >&);

      PSI_STD::vector<TreePtr<FunctionTypeArgument> > arguments;
      TreePtr<Type> result_type;

      template<typename Visitor> static void visit_impl(FunctionType&, Visitor&);
    };

    class FunctionArgument : public Value {
    public:
      FunctionArgument(const TreePtr<Type>&, const SourceLocation&);
      virtual ~FunctionArgument();
    };

    class Function : public Tree {
    public:
      Function(const TreePtr<FunctionType>&, const SourceLocation&);
      Function(const TreePtr<FunctionType>&, const SourceLocation&, DependencyPtr&);
      virtual ~Function();

      PSI_STD::vector<TreePtr<FunctionArgument> > arguments;
      TreePtr<> result_type;
      TreePtr<> body;

      template<typename Visitor> static void visit_impl(Function&, Visitor&);
    };

    class TryFinally : public Tree {
    public:
      TryFinally(const TreePtr<Type>&, const SourceLocation&);
      virtual ~TryFinally();

      TreePtr<> try_block, finally_block;

      template<typename Visitor> static void visit_impl(TryFinally&, Visitor&);
    };
  }
}

#endif
