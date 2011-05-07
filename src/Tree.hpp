#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief Base class for all types used to represent code and data.
     */
    class Tree : public CompileObject {
      bool m_canonical_computed;
      TreePtr<> m_canonical_form;

      void compute_canonical_form();
      
    protected:
      virtual void gc_visit(GCVisitor&);
      virtual TreePtr<> rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions);

    public:
      Tree(CompileContext&);
      virtual ~Tree() = 0;

      TreePtr<> rewrite(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);

      TreePtr<> canonical() {
        if (!m_canonical_computed)
          compute_canonical_form();
        return m_canonical_form;
      }

      GCPtr<Future> dependency;
      TreePtr<Type> type;
    };

    /**
     * \brief Base class for type trees.
     */
    class Type : public Tree {
    protected:
      Type(CompileContext&);
      virtual void gc_visit(GCVisitor&);
    public:
      virtual ~Type();
    };

    /**
     * \brief Tree for a statement, which should be part of a block.
     */
    class Statement : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Statement(CompileContext&);
      virtual ~Statement();

      TreePtr<Statement> next;
      TreePtr<> value;
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Block(CompileContext&);
      virtual ~Block();

      TreePtr<Statement> statements;
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
      EmptyValue(const TreePtr<EmptyType>&);
      virtual ~EmptyValue();
    };

    class UnaryOperation : public Tree {
    protected:
      UnaryOperation(CompileContext&);
      UnaryOperation(const UnaryOperation&);
      virtual void gc_visit(GCVisitor&);
      typedef UnaryOperation RewriteDuplicateType;
      virtual TreePtr<UnaryOperation> rewrite_duplicate_hook() = 0;

    public:
      virtual ~UnaryOperation();
      virtual TreePtr<> rewrite_hook(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);

      TreePtr<> child;
    };

    class BinaryOperation : public Tree {
    protected:
      BinaryOperation(CompileContext&);
      BinaryOperation(const BinaryOperation&);
      virtual void gc_visit(GCVisitor&);
      typedef BinaryOperation RewriteDuplicateType;
      virtual TreePtr<BinaryOperation> rewrite_duplicate_hook() = 0;

    public:
      virtual ~BinaryOperation();
      virtual TreePtr<> rewrite_hook(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);

      TreePtr<> left, right;
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
      virtual TreePtr<> rewrite_hook(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);

      TreePtr<Type> argument_type_after(const SourceLocation&, const std::vector<TreePtr<> >&);
      TreePtr<Type> result_type_after(const SourceLocation&, const std::vector<TreePtr<> >&);

      std::vector<TreePtr<FunctionTypeArgument> > arguments;
      TreePtr<Type> result_type;
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

      std::vector<TreePtr<FunctionArgument> > arguments;
      TreePtr<> result_type;
      TreePtr<> body;
    };

    class TryFinally : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      TryFinally(CompileContext&);
      virtual ~TryFinally();

      TreePtr<> try_block, finally_block;
    };
  }
}

#endif
