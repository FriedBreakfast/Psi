#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Compiler {
    class CompileImplementation;
    
    /**
     * \brief Base class for all types used to represent code and data.
     */
    class Tree : public GCBase {
      enum CompletionState {
        completion_constructed,
        completion_running,
        completion_finished,
        completion_failed
      };
      
      CompileContext *m_compile_context;
      SourceLocation m_location;
      TreePtr<Type> m_type;
      CompletionState m_completion_state;
      Dependency* m_dependency;
      void complete_main();
      
    protected:
      virtual void gc_visit(GCVisitor&);
      virtual void gc_destroy();
      virtual TreePtr<> rewrite_hook(const SourceLocation& location, const std::map<TreePtr<>, TreePtr<> >& substitutions);

    public:
      Tree(CompileContext&, const SourceLocation&, DependencyPtr&);
      Tree(const TreePtr<Type>&, const SourceLocation&, DependencyPtr&);
      Tree(CompileContext&, const SourceLocation&);
      Tree(const TreePtr<Type>&, const SourceLocation&);
      virtual ~Tree() = 0;

      /// \brief Return the compilation context this tree belongs to.
      CompileContext& compile_context() const {return *m_compile_context;}
      /// \brief Get the location associated with this tree
      const SourceLocation& location() const {return m_location;}
      /// \brief Get the type of this tree
      const TreePtr<Type>& type() const {return m_type;}
      void complete();
      void dependency_complete();
      TreePtr<> rewrite(const SourceLocation&, const std::map<TreePtr<>, TreePtr<> >&);
    };

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
    
    class CompileImplementation : public Tree {
      virtual void gc_visit(GCVisitor&);
    public:
      CompileImplementation(CompileContext&, const SourceLocation&);
      CompileImplementation(CompileContext&, const SourceLocation&, DependencyPtr&);
      TreePtr<> vtable;
    };

    template<typename Wrapper>
    Wrapper compile_implementation_wrap(const TreePtr<CompileImplementation>& impl) {
      if (!impl)
        return Wrapper();
      Wrapper wrapper;
      wrapper.vptr = static_cast<typename Wrapper::VtableType*>(impl->compile_context().jit_compile(wrapper.data));
      wrapper.data = impl;
      return wrapper;
    }

    template<typename Wrapper>
    Wrapper compile_implementation_wrap(const TreePtr<>& impl, const SourceLocation& location) {
      if (!impl)
        return Wrapper();
      TreePtr<CompileImplementation> cast_impl = dynamic_pointer_cast<CompileImplementation>(impl);
      if (!cast_impl)
        impl->compile_context().error_throw(location, "Could not cast");
      return compile_implementation_wrap<Wrapper>(cast_impl);
    }

    template<typename Wrapper>
    Wrapper compile_implementation_lookup(const TreePtr<Interface>& interface, const TreePtr<>& parameter, CompileContext& compile_context, const SourceLocation& location) {
      return compile_implementation_wrap<Wrapper>(interface_lookup(interface, parameter, compile_context, location), location);
    }

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
      Statement(const TreePtr<>&);
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
      Block(const TreePtr<Type>&, const SourceLocation&, DependencyPtr&);
      virtual ~Block();

      std::vector<TreePtr<Statement> > statements;
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
      FunctionTypeArgument(const TreePtr<Type>&, const SourceLocation&);
      virtual ~FunctionTypeArgument();
    };

    class FunctionType : public Type {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      FunctionType(CompileContext&, const SourceLocation&);
      FunctionType(CompileContext&, const SourceLocation&, DependencyPtr&);
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
      FunctionArgument(const TreePtr<Type>&, const SourceLocation&);
      virtual ~FunctionArgument();
    };

    class Function : public Tree {
    protected:
      virtual void gc_visit(GCVisitor&);

    public:
      Function(const TreePtr<FunctionType>&, const SourceLocation&);
      Function(const TreePtr<FunctionType>&, const SourceLocation&, DependencyPtr&);
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
