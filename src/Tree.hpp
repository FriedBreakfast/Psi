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

      template<typename Visitor>
      static void visit_impl(Parameter& self, Visitor& visitor) {
	Term::visit_impl(self, visitor);
	visitor
	  ("depth", self.depth)
	  ("index", self.index);
      }
    };
    
    class Implementation : public Tree {
    public:
      static const TreeVtable vtable;

      Implementation(CompileContext&, const SourceLocation&);

      TreePtr<Interface> interface;
      TreePtr<> value;
      /// \brief Pattern match variable types.
      PSI_STD::vector<TreePtr<Term> > wildcard_types;
      /// \brief Parameters to the interface type
      PSI_STD::vector<TreePtr<Term> > interface_parameters;

      template<typename Visitor>
      static void visit_impl(Implementation& self, Visitor& visitor) {
	Tree::visit_impl(self, visitor);
	visitor
	  ("interface", self.interface)
	  ("value", self.value)
	  ("wildcard_types", self.wildcard_types)
	  ("interface_parameters", self.interface_parameters);
      }
    };

    /**
     * \brief Base type for terms which carry interface implementations.
     */
    class ImplementationTerm : public Term {
    public:
      ImplementationTerm(const TreePtr<Term>&, const SourceLocation&);
      PSI_STD::vector<TreePtr<Implementation> > implementations;

      template<typename Visitor>
      static void visit_impl(ImplementationTerm& self, Visitor& visitor) {
	Term::visit_impl(self, visitor);
	visitor
	  ("implementations", self.implementations);
      }
    };
    
    class Global : public Term {
      friend class CompileContext;
      void *m_jit_ptr;
    public:
      Global(const TreePtr<Type>&, const SourceLocation&);
    };
    
    class ExternalGlobal : public Global {
    public:
      static const TermVtable vtable;
      ExternalGlobal(const TreePtr<Type>&, const SourceLocation&);
      String symbol;

      template<typename Visitor>
      static void visit_impl(ExternalGlobal& self, Visitor& visitor) {
	Global::visit_impl(self, visitor);
	visitor
	  ("symbol", self.symbol);
      }
    };

    /**
     * \brief Entry in a Block.
     */
    class Statement : public Term {
    public:
      static const TermVtable vtable;
      Statement(const TreePtr<Term>&, const SourceLocation&);
      TreePtr<Term> value;

      template<typename Visitor>
      static void visit_impl(Statement& self, Visitor& visitor) {
	Term::visit_impl(self, visitor);
	visitor("value", self.value);
      }
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Term {
    public:
      static const TermVtable vtable;

      Block(const TreePtr<Term>&, const SourceLocation&);

      PSI_STD::vector<TreePtr<Statement> > statements;
      TreePtr<Term> result;
      DependencyPtr dependency;

      template<typename Visitor>
      static void visit_impl(Block& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor
	  ("statements", self.statements)
          ("result", self.result)
          ("dependency", self.dependency);
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
      static const TermVtable vtable;

      RecursiveType(CompileContext&, const SourceLocation&);

      /// \brief
      PSI_STD::vector<TreePtr<Term> > parameters;
      /// \brief Single member of this type.
      TreePtr<Term> member;

      template<typename Visitor>
      static void visit_impl(RecursiveType& self, Visitor& visitor) {
        ImplementationTerm::visit_impl(self, visitor);
        visitor
	  ("parameters", self.parameters)
          ("member", self.member);
      }
    };

    class SpecializedRecursiveType : public Term {
    };

    /**
     * \brief Value of type RecursiveType.
     */
    class RecursiveValue : public Term {
      TreePtr<Term> m_member;
      RecursiveValue(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&);
    public:
      static const TermVtable vtable;

      static TreePtr<Term> get(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&);

      template<typename Visitor>
      static void visit_impl(RecursiveValue& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
      }
    };

    /**
     * \brief Type of types.
     */
    class Metatype : public Term {
      friend class CompileContext;
      Metatype(CompileContext&, const SourceLocation&);
    public:
      static const TermVtable vtable;
    };

    /**
     * \brief Empty type.
     *
     * Unions and structures with no members can be replaced by this type.
     */
    class EmptyType : public Type {
      friend class CompileContext;
      EmptyType(CompileContext&, const SourceLocation&);
    public:
      static const TermVtable vtable;
      static TreePtr<Term> value(CompileContext&, const SourceLocation&);
    };

    /**
     * \brief Zero initialized value.
     */
    class NullValue : public Term {
      NullValue(const TreePtr<Term>&, const SourceLocation&);
    public:
      static const TermVtable vtable;
      static TreePtr<Term> get(const TreePtr<Term>&, const SourceLocation&);
    };

    /**
     * \brief Structure type.
     *
     * Stores a list of other types.
     */
    class StructType : public Type {
      StructType(CompileContext&);
    public:
      static const TermVtable vtable;
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
      static const TermVtable vtable;
      static TreePtr<Term>& get(CompileContext&, const PSI_STD::vector<TreePtr<Term> >&);
    };

    class FunctionTypeArgument : public Term {
    public:
      static const TermVtable vtable;
      FunctionTypeArgument(const TreePtr<Term>&, const SourceLocation&);
    };

    class FunctionType : public Type {
    public:
      static const TermVtable vtable;

      FunctionType(CompileContext&, const SourceLocation&);
      static TreePtr<Term> rewrite_impl(FunctionType&, const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);

      TreePtr<Term> argument_type_after(const SourceLocation&, const List<TreePtr<Term> >&);
      TreePtr<Term> result_type_after(const SourceLocation&, const List<TreePtr<Term> >&);

      PSI_STD::vector<TreePtr<FunctionTypeArgument> > arguments;
      TreePtr<Term> result_type;

      template<typename Visitor>
      static void visit_impl(FunctionType& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
	visitor
	  ("arguments", self.arguments)
	  ("result_type", self.result_type);
      }
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
        visitor
	  ("arguments", self.arguments)
	  ("result_type", self.result_type)
	  ("body", self.body);
      }
      
      static void complete_callback_impl(Function&);
    };

    class TryFinally : public Term {
    public:
      TryFinally(const TreePtr<Term>&, const SourceLocation&);

      TreePtr<Term> try_expr, finally_expr;

      template<typename Visitor> static void visit_impl(TryFinally& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor
	  ("try_expr", self.try_expr)
	  ("finally_expr", self.finally_expr);
      }
    };
  }
}

#endif
