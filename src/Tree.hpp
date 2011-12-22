#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * Anonymous term. Has a type but no defined value.
     *
     * The value must be defined elsewhere, for example by being part of a function.
     */
    class Anonymous : public Term {
    public:
      static const TermVtable vtable;

      Anonymous(const TreePtr<Term>& type, const SourceLocation& location);
    };
    
    class Parameter : public Term {
    public:
      static const TermVtable vtable;

      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location);

      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned depth;
      /// Index of this parameter in its scope.
      unsigned index;

      template<typename Visitor> static void visit(Visitor& v);
    };
    
    class Implementation : public Tree {
    public:
      static const TreeVtable vtable;

      Implementation(CompileContext& compile_context,
                     const TreePtr<>& value,
                     const TreePtr<Interface>& interface,
                     const PSI_STD::vector<TreePtr<Term> >& wildcard_types,
                     const PSI_STD::vector<TreePtr<Term> >& interface_parameters,
                     const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<> value;
      TreePtr<Interface> interface;
      /// \brief Pattern match variable types.
      PSI_STD::vector<TreePtr<Term> > wildcard_types;
      /// \brief Parameters to the interface type
      PSI_STD::vector<TreePtr<Term> > interface_parameters;

      bool matches(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) const;
    };

    class Global : public Term {
    public:
      static const SIVtable vtable;
      Global(const TreePtr<Term>& type, const SourceLocation& location);

      mutable void *jit_ptr;
    };
    
    class ExternalGlobal : public Global {
      String m_symbol;

    public:
      static const TermVtable vtable;
      ExternalGlobal(const TreePtr<Term>& type, const String& symbol, const SourceLocation& location);

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Global>(v);
        v("symbol", &ExternalGlobal::m_symbol);
      }
    };

    /**
     * \brief Entry in a Block.
     */
    class Statement : public Term {
    public:
      static const TermVtable vtable;

      TreePtr<Term> value;
      
      Statement(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public Term {
    public:
      static const TermVtable vtable;
      
      PSI_STD::vector<TreePtr<Statement> > statements;
      TreePtr<Term> value;

      Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
    };

    /**
     * \brief Named, recursive type.
     *
     * This class allows the creation of types which contain references
     * to themselves. In order for these to work these types are by
     * definition unique, however template parameters are also available.
     *
     * It derives from Tree not Term because it cannot be used as a type
     * itself, it can only be used as a type through TypeInstance.
     */
    class GenericType : public Tree {
    public:
      static const TreeVtable vtable;

      GenericType(const TreePtr<Term>& member,
                  const PSI_STD::vector<TreePtr<Parameter> >& parameters,
                  const PSI_STD::vector<TreePtr<Implementation> >& implementations,
                  const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);

      /// \brief Single member of this type.
      TreePtr<Term> member;
      /// \brief
      PSI_STD::vector<TreePtr<Parameter> > parameters;
      /// \brief Implementations carried by this type.
      PSI_STD::vector<TreePtr<Implementation> > implementations;
    };

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Term {
    public:
      static const TermVtable vtable;

      TypeInstance(const TreePtr<GenericType>& generic_type,
                   const PSI_STD::vector<TreePtr<Term> >& parameter_values,
                   const SourceLocation& location);

      TreePtr<GenericType> generic_type;
      /// \brief Arguments to parameters in RecursiveType
      PSI_STD::vector<TreePtr<Term> > parameter_values;

      template<typename Visitor> static void visit(Visitor& v);
      static TreePtr<> interface_search_impl(const TypeInstance& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
    };

    /**
     * \brief Value of type RecursiveType.
     */
    class TypeInstanceValue : public Term {
      TreePtr<Term> m_member_value;

    public:
      static const TermVtable vtable;

      TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value, const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);
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
    public:
      static const TermVtable vtable;
      NullValue(const TreePtr<Term>&, const SourceLocation&);
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

    class FunctionType : public Type {
      
    public:
      static const TermVtable vtable;

      FunctionType(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Anonymous> >& arguments, const SourceLocation& location);

      TreePtr<Term> argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const;
      
      TreePtr<Term> result_type;
      PSI_STD::vector<TreePtr<Term> > argument_types;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Type>(v);
        v("result_type", &FunctionType::result_type)
        ("argument_types", &FunctionType::argument_types);
      }
    };

    class Function : public Term {
    public:
      static const TermVtable vtable;
      
      Function(const TreePtr<Term>& result_type,
               const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
               const TreePtr<Term>& body,
               const SourceLocation& location);

      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      TreePtr<Term> result_type;
      TreePtr<Term> body;

      template<typename Visitor> static void visit(Visitor& v);
    };

    /**
     * \brief Try-finally.
     */
    class TryFinally : public Term {
    public:
      static const TermVtable vtable;

      TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      TreePtr<Term> try_expr, finally_expr;
    };

    /**
     * \brief Function invocation expression.
     */
    class FunctionCall : public Term {
      TreePtr<Term> get_type(const TreePtr<Term>& target, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

    public:
      static const TermVtable vtable;

      FunctionCall(const TreePtr<Term>& target, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      TreePtr<Term> target;
      PSI_STD::vector<TreePtr<Term> > arguments;
    };
  }
}

#endif
