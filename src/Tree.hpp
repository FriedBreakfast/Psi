#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    class Parameter : public Term {
      friend class Term;

      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned m_depth;
      /// Index of this parameter in its scope.
      unsigned m_index;
      
    public:
      static const TermVtable vtable;

      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location);

      template<typename Visitor> static void visit_impl(Parameter& self, Visitor& visitor);

      class PtrHook : public Term::PtrHook {
        Parameter* get() const {return ptr_as<Parameter>();}

      public:
        /// \copydoc m_depth
        unsigned depth() const {return get()->m_depth;}
        /// \copydoc m_index
        unsigned index() const {return get()->m_index;}
      };
    };
    
    class Implementation : public Tree {
      TreePtr<Interface> m_interface;
      TreePtr<> m_value;
      PSI_STD::vector<TreePtr<Term> > m_wildcard_types;
      PSI_STD::vector<TreePtr<Term> > m_interface_parameters;

    public:
      static const TreeVtable vtable;

      Implementation(CompileContext& compile_context,
                     const TreePtr<>& value,
                     const PSI_STD::vector<TreePtr<Term> >& wildcard_types,
                     const PSI_STD::vector<TreePtr<Term> >& interface_parameters,
                     const SourceLocation& location);
      template<typename Visitor> static void visit_impl(Implementation& self, Visitor& visitor);

      class PtrHook : public Tree::PtrHook {
        Implementation* get() const {return ptr_as<Implementation>();}

      public:
        const TreePtr<Interface>& interface() const {return get()->m_interface;}
        const TreePtr<>& value() const {return get()->m_value;}
        /// \brief Pattern match variable types.
        const PSI_STD::vector<TreePtr<Term> > wildcard_types() const {return get()->m_wildcard_types;}
        /// \brief Parameters to the interface type
        const PSI_STD::vector<TreePtr<Term> > interface_parameters() const {return get()->m_interface_parameters;}
      };
    };

    /**
     * \brief Base type for terms which carry interface implementations.
     */
    class ImplementationTerm : public Term {
      PSI_STD::vector<TreePtr<Implementation> > m_implementations;

    public:
      static const SIVtable vtable;

      ImplementationTerm(const TreePtr<Term>& type,
                         const PSI_STD::vector<TreePtr<Implementation> >& implementations,
                         const SourceLocation& location);

      class PtrHook : public Tree::PtrHook {
        ImplementationTerm* get() const {return ptr_as<ImplementationTerm>();}

      public:
        /// \brief Get the list of implementations supplied by this term.
        const PSI_STD::vector<TreePtr<Implementation> >& implementations() const {return get()->m_implementations;}
      };

      template<typename Visitor>
      static void visit_impl(ImplementationTerm& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor
          ("implementations", self.m_implementations);
      }
    };
    
    class Global : public Term {
      friend class CompileContext;
      void *m_jit_ptr;
    public:
      static const SIVtable vtable;
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
      TreePtr<Term> m_value;
      
    public:
      static const TermVtable vtable;
      
      Statement(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit_impl(Statement& self, Visitor& visitor);
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Term {
      PSI_STD::vector<TreePtr<Statement> > m_statements;
      TreePtr<Term> m_value;

    public:
      static const TermVtable vtable;

      Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit_impl(Block& self, Visitor& visitor);
    };

    /**
     * \brief Named, recursive type.
     *
     * This class allows the creation of types which contain references
     * to themselves. In order for these to work these types are by
     * definition unique, however template parameters are also available.
     */
    class RecursiveType : public ImplementationTerm {
      /// \brief
      PSI_STD::vector<TreePtr<Term> > m_parameters;
      /// \brief Single member of this type.
      TreePtr<Term> m_member;

    public:
      static const TermVtable vtable;

      RecursiveType(CompileContext& compile_context, const SourceLocation& location);

      template<typename Visitor>
      static void visit_impl(RecursiveType& self, Visitor& visitor) {
        ImplementationTerm::visit_impl(self, visitor);
        visitor
          ("parameters", self.m_parameters)
          ("member", self.m_member);
      }
    };

    class SpecializedRecursiveType : public Term {
    public:
      static const TermVtable vtable;
    };

    /**
     * \brief Value of type RecursiveType.
     */
    class RecursiveValue : public Term {
      RecursiveValue(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&);
    public:
      static const TermVtable vtable;
      static TreePtr<Term> get(const TreePtr<RecursiveType>&, const TreePtr<Term>&, const SourceLocation&);

      TreePtr<Term> member;

      template<typename Visitor>
      static void visit_impl(RecursiveValue& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor("member", self.member);
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
      TreePtr<Term> m_result_type;
      PSI_STD::vector<TreePtr<FunctionTypeArgument> > m_arguments;
      
    public:
      static const TermVtable vtable;

      FunctionType(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<FunctionTypeArgument> >& arguments, const SourceLocation& location);
      static TreePtr<Term> rewrite_impl(FunctionType&, const SourceLocation&, const Map<TreePtr<Term>, TreePtr<Term> >&);

      TreePtr<Term> argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments);
      TreePtr<Term> result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments);
      
      class PtrHook : public Type::PtrHook {
        FunctionType *get() const {return ptr_as<FunctionType>();}
      public:
        const PSI_STD::vector<TreePtr<FunctionTypeArgument> >& arguments() const {return get()->m_arguments;}
        const TreePtr<Term>& result_type() const {return get()->m_result_type;}
        TreePtr<Term> argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const {return get()->argument_type_after(location, arguments);}
        TreePtr<Term> result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const {return get()->result_type_after(location, arguments);}
      };

      template<typename Visitor>
      static void visit_impl(FunctionType& self, Visitor& visitor) {
        Term::visit_impl(self, visitor);
        visitor
          ("result_type", self.m_result_type)
          ("arguments", self.m_arguments);
      }
    };

    class FunctionArgument : public Term {
    public:
      static const TermVtable vtable;
      FunctionArgument(const TreePtr<Term>&, const SourceLocation&);
    };

    class Function : public Term {
      PSI_STD::vector<TreePtr<FunctionArgument> > m_arguments;
      TreePtr<Term> m_result_type;
      TreePtr<Term> m_body;

      static TreePtr<Term> get_type(const TreePtr<Term>& result_type,
                                    const PSI_STD::vector<TreePtr<FunctionArgument> >& arguments);

    public:
      static const TermVtable vtable;
      
      Function(const TreePtr<Term>& result_type,
               const PSI_STD::vector<TreePtr<FunctionArgument> >& arguments,
               const TreePtr<Term>& body,
               const SourceLocation& location);

      class PtrHook : public Term::PtrHook {
      public:
        const PSI_STD::vector<TreePtr<FunctionArgument> >& arguments() const {return ptr_as<Function>()->m_arguments;}
        const TreePtr<Term>& result_type() const {return ptr_as<Function>()->m_result_type;}
        const TreePtr<Term>& body() const {return ptr_as<Function>()->m_body;}
      };

      template<typename Visitor> static void visit_impl(Function& self, Visitor& visitor);
    };

    class TryFinally : public Term {
      TreePtr<Term> m_try_expr, m_finally_expr;
      
    public:
      static const TermVtable vtable;

      TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, const SourceLocation& location);
      template<typename Visitor> static void visit_impl(TryFinally& self, Visitor& visitor);
    };
  }
}

#endif
