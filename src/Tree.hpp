#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    class Parameter : public Term {
    public:
      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned depth;
      /// Index of this parameter in its scope.
      unsigned index;
    };
    
    class Implementation : public Tree {
    public:
      Implementation(CompileContext&, const SourceLocation&);

      TreePtr<Interface> interface;
      TreePtr<> value;
      /// \brief Pattern match variable types.
      PSI_STD::vector<TreePtr<Term> > wildcard_types;
      /// \brief Parameters to the interface type
      PSI_STD::vector<TreePtr<Term> > interface_parameters;
    };

    /**
     * \brief Base type for terms which carry interface implementations.
     */
    class ImplementationTerm : public Term {
    public:
      ImplementationTerm(const TreePtr<Term>&, const SourceLocation&);
      PSI_STD::vector<TreePtr<Implementation> > implementations;
    };
    
    class GlobalTree : public Term {
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
     * \brief Tree for a block of code.
     */
    class Block : public Term {
    public:
      Block(const TreePtr<Term>&, const SourceLocation&);

      PSI_STD::vector<TreePtr<Term> > statements;
      TreePtr<Term> result;
      DependencyPtr dependency;

      template<typename Visitor> static void visit_impl(Block& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor("statements", self.statements);
        visitor("result", self.result);
        visitor("dependency", self.dependency);
      }
    };

    /**
     * \brief Named, recursive type.
     *
     * This class allows the creation of types which contain references
     * to themselves. In order for these to work these types are by
     * definition unique, however template parameters are also available.
     */
    class RecursiveType : public ImplementationTerm {
    public:
      RecursiveType(CompileContext&, const SourceLocation&);

      /// \brief
      PSI_STD::vector<TreePtr<> > parameters;
      /// \brief Single member of this type.
      TreePtr<Term> member;
    };

    /**
     * \brief Value of type RecursiveType.
     */
    class RecursiveValue : public Term {
      TreePtr<Term> m_member;
      RecursiveValue(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&);
    public:
      static TreePtr<Term> get(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&);
    };

    /**
     * \brief Empty type.
     *
     * Unions and structures with no members can be replaced by this type.
     */
    class EmptyType : public Type {
      EmptyType(CompileContext&, const SourceLocation&);
    public:
      static TreePtr<Term> get(CompileContext&);
    };

    /**
     * \brief Empty value.
     *
     * Value of type EmptyType.
     */
    class EmptyValue : public Term {
      EmptyValue(CompileContext&, const SourceLocation&);
    public:
      static TreePtr<Term> get(CompileContext&);
    };

    /**
     * \brief Structure type.
     *
     * Stores a list of other types.
     */
    class StructType : public Type {
      StructType(CompileContext&);
    public:
      static TreePtr<Term>& get(CompileContext&, const PSI_STD::vector<TreePtr<Term> >&);
    };

    /**
     * \brief Union type.
     *
     * Stores one out of a list of other fields. This does not know which field
     * is actually held; that information must be kept elsewhere.
     */
    class UnionType : public Type {
      UnionType(CompileContext&);
    public:
      static TreePtr<Term>& get(CompileContext&, const PSI_STD::vector<TreePtr<Term> >&);
    };

    class FunctionTypeArgument : public Term {
    public:
      FunctionTypeArgument(const TreePtr<Term>&, const SourceLocation&);
    };

    class FunctionType : public Type {
    public:
      FunctionType(CompileContext&, const SourceLocation&);
      TreePtr<Term> rewrite_impl(const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);

      TreePtr<Term> argument_type_after(const SourceLocation&, const List<TreePtr<Term> >&);
      TreePtr<Term> result_type_after(const SourceLocation&, const List<TreePtr<Term> >&);

      PSI_STD::vector<TreePtr<FunctionTypeArgument> > arguments;
      TreePtr<Term> result_type;

      template<typename Visitor> static void visit_impl(FunctionType&, Visitor&);
    };

    class FunctionArgument : public Term {
    public:
      FunctionArgument(const TreePtr<Term>&, const SourceLocation&);
    };

    class Function : public Term {
      DependencyPtr m_dependency;
      
    public:
      static const TermVtable vtable;
      
      Function(const TreePtr<FunctionType>&, const SourceLocation&);
      Function(const TreePtr<FunctionType>&, const SourceLocation&, DependencyPtr&);

      PSI_STD::vector<TreePtr<FunctionArgument> > arguments;
      TreePtr<> result_type;
      TreePtr<> body;

      template<typename Visitor> static void visit_impl(Function& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor("arguments", self.arguments);
        visitor("result_type", self.result_type);
        visitor("body", self.body);
      }
      
      static void complete_callback_impl(Function&);
    };

    class TryFinally : public Term {
    public:
      TryFinally(const TreePtr<Term>&, const SourceLocation&);

      TreePtr<Term> try_expr, finally_expr;

      template<typename Visitor> static void visit_impl(TryFinally& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor("try_expr", self.try_expr);
        visitor("finally_expr", self.finally_expr);
      }
    };
  }
}

#endif
