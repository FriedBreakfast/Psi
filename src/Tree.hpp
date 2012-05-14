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

      Anonymous(CompileContext& compile_context, const SourceLocation& location);
      Anonymous(const TreePtr<Term>& type, const SourceLocation& location);
    };
    
    class Parameter : public Term {
    public:
      static const TermVtable vtable;

      Parameter(CompileContext& compile_context, const SourceLocation& location);
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

      Implementation(CompileContext& compile_context, const SourceLocation& location);
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
      Global(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
      Global(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location);
    };
    
    class ExternalGlobal : public Global {
      String m_symbol;

    public:
      static const TermVtable vtable;
      ExternalGlobal(CompileContext& compile_context, const SourceLocation& location);
      ExternalGlobal(const TreePtr<Term>& type, const String& symbol, const SourceLocation& location);

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Global>(v);
        v("symbol", &ExternalGlobal::m_symbol);
      }
    };
    
    /**
     * Wrapper around entries in \c StatementList.
     */
    class Statement : public Term {
    public:
      static const TermVtable vtable;

      TreePtr<Term> value;
      
      Statement(CompileContext& compile_context, const SourceLocation& location);
      Statement(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static TreePtr<> interface_search_impl(const Statement& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
    };
    
    /**
     * \brief Base class for terms which are lists of other terms.
     */
    class StatementList : public Term {
    public:
      static const SIVtable vtable;
      
      PSI_STD::vector<TreePtr<Statement> > statements;

      StatementList(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location);
      StatementList(const TermVtable *vptr, const TreePtr<Term>& type, const PSI_STD::vector<TreePtr<Statement> >& statements, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
    };

    /**
     * \brief Tree for a block of code.
     */
    class Block : public StatementList {
    public:
      static const TermVtable vtable;
      
      TreePtr<Term> value;

      Block(CompileContext& compile_context, const SourceLocation& location);
      Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
    };
    
    /**
     * \brief Tree for a namespace.
     */
    class Namespace : public StatementList {
    public:
      static const TermVtable vtable;

      Namespace(CompileContext& compile_context, const SourceLocation& location);
      Namespace(const PSI_STD::vector<TreePtr<Statement> >& statements, CompileContext& compile_context, const SourceLocation& location);
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

      GenericType(CompileContext& compile_context, const SourceLocation& location);
      GenericType(const TreePtr<Term>& member,
                  const PSI_STD::vector<TreePtr<Anonymous> >& parameters,
                  const PSI_STD::vector<TreePtr<Implementation> >& implementations,
                  const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);

      /// \brief Single member of this type.
      TreePtr<Term> member;
      /// \brief Implementations carried by this type.
      PSI_STD::vector<TreePtr<Implementation> > implementations;

      TreePtr<GenericType> parameterize(const SourceLocation& location, unsigned depth);
      TreePtr<> specialize(const SourceLocation& location, unsigned depth);
    };

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Term {
    public:
      static const TermVtable vtable;

      TypeInstance(CompileContext& compile_context, const SourceLocation& location);
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

      TypeInstanceValue(CompileContext& compile_context, const SourceLocation& location);
      TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value, const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);
    };
    
    /**
     * \brief Bottom type.
     * 
     * This type cannot be instantiated, hence any expression of this type
     * cannot return.
     */
    class BottomType : public Type {
      friend class CompileContext;
    public:
      static const TermVtable vtable;
      static TreePtr<Term> value(CompileContext& compile_context, const SourceLocation& location);

      BottomType(CompileContext&, const SourceLocation&);
    };

    /**
     * \brief Empty type.
     *
     * Unions and structures with no members can be replaced by this type.
     */
    class EmptyType : public Type {
      friend class CompileContext;
    public:
      static const TermVtable vtable;
      static TreePtr<Term> value(CompileContext& compile_context, const SourceLocation& location);

      EmptyType(CompileContext&, const SourceLocation&);
    };

    /**
     * \brief Zero initialized value.
     */
    class NullValue : public Term {
    public:
      static const TermVtable vtable;
      NullValue(CompileContext& compile_context, const SourceLocation& location);
      NullValue(const TreePtr<Term>& type, const SourceLocation& location);
    };

    /**
     * \brief Structure type.
     *
     * Stores a list of other types.
     */
    class StructType : public Type {
    public:
      static const TermVtable vtable;
      StructType(CompileContext& compile_context, const SourceLocation& location);
      StructType(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);

      PSI_STD::vector<TreePtr<Term> > members;
    };

    /**
     * \brief Structure value.
     */
    class StructValue : public Term {
    public:
      static const TermVtable vtable;
      StructValue(CompileContext& compile_context, const SourceLocation& location);
      StructValue(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);

      PSI_STD::vector<TreePtr<Term> > members;
    };

    /**
     * \brief Union type.
     *
     * Stores one out of a list of other fields. This does not know which field
     * is actually held; that information must be kept elsewhere.
     */
    class UnionType : public Type {
    public:
      static const TermVtable vtable;
      UnionType(CompileContext& compile_context, const SourceLocation& location);
      UnionType(CompileContext& compile_context, const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& members);

      PSI_STD::vector<TreePtr<Term> > members;
    };

    class FunctionType : public Type {
    public:
      static const TermVtable vtable;

      FunctionType(CompileContext& compile_context, const SourceLocation& location);
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

    class Function : public Global {
    public:
      static const TermVtable vtable;
      
      Function(CompileContext& compile_context, const SourceLocation& location);
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

      TryFinally(CompileContext& compile_context, const SourceLocation& location);
      TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      TreePtr<Term> try_expr, finally_expr;
    };
    
    class TryCatch : public Term {
    public:
      static const TermVtable vtable;
      
      TryCatch(CompileContext& compile_context, const SourceLocation& location);
      TryCatch(const TreePtr<Term>& try_clause,
               const PSI_STD::vector<TreePtr<Anonymous> >& catch_parameters,
               const TreePtr<Term>& catch_clause,
               const SourceLocation& location);
      
      TreePtr<Term> try_clause;
      PSI_STD::vector<TreePtr<Anonymous> > catch_parameters;
      TreePtr<Term> catch_clause;
    };
    
    /**
     * \brief If-Then-Else.
     */
    class IfThenElse : public Term {
    public:
      static const TermVtable vtable;
      
      IfThenElse(CompileContext& compile_context, const SourceLocation& location);
      IfThenElse(const TreePtr<Term>& condition, const TreePtr<Term>& true_value, const TreePtr<Term>& false_value, const SourceLocation& location);
      
      TreePtr<Term> condition;
      TreePtr<Term> true_value;
      TreePtr<Term> false_value;
    };
    
    /**
     * \brief Jump group entry.
     * 
     * Target of JumpTo instruction.
     */
    class JumpGroupEntry : public Tree {
    public:
      static const TreeVtable vtable;
      
      JumpGroupEntry(CompileContext& compile_context, const SourceLocation& location);
      JumpGroupEntry(const TreePtr<Term>& value, const TreePtr<Anonymous>& argument, const SourceLocation& location);
      
      TreePtr<Term> value;
      TreePtr<Anonymous> argument;
    };
    
    /**
     * \brief Jump group.
     * 
     * Allows a structured JumpTo instruction to exist: the jump instruction unwinds to this
     * group and then causes the selected branch to be run (used as the result value).
     * 
     * All control constructs are implemented using this class.
     */
    class JumpGroup : public Term {
    public:
      static const TermVtable vtable;
      
      JumpGroup(CompileContext& compile_context, const SourceLocation& location);
      JumpGroup(const TreePtr<Term>& initial, const PSI_STD::vector<TreePtr<JumpGroupEntry> >& values, const SourceLocation& location);
      
      TreePtr<Term> initial;
      PSI_STD::vector<TreePtr<JumpGroupEntry> > entries;
    };
    
    /**
     * \brief Jump instruction.
     * 
     * Unwinds to the specified jump group and then takes the specified branch.
     */
    class JumpTo : public Term {
    public:
      static const TermVtable vtable;
      
      JumpTo(CompileContext& compile_context, const SourceLocation& location);
      JumpTo(const TreePtr<JumpGroupEntry>& target, const TreePtr<Term>& argument, const SourceLocation& location);
      
      TreePtr<JumpGroupEntry> target;
      TreePtr<Term> argument;
    };

    /**
     * \brief Function invocation expression.
     */
    class FunctionCall : public Term {
      TreePtr<Term> get_type(const TreePtr<Term>& target, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

    public:
      static const TermVtable vtable;

      FunctionCall(CompileContext& compile_context, const SourceLocation& location);
      FunctionCall(const TreePtr<Term>& target, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      TreePtr<Term> target;
      PSI_STD::vector<TreePtr<Term> > arguments;
    };
    
    /**
     * Tree for builtin types.
     * 
     * This saves having to create a separate tree for each one, at least until later in the compilation process
     * so that a uniform syntax may be used by the user.
     */
    class BuiltinType : public Type {
    public:
      static const TermVtable vtable;
      
      BuiltinType(CompileContext& compile_context, const SourceLocation& location);
      BuiltinType(CompileContext& compile_context, const String& name, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      static TreePtr<> interface_search_impl(BuiltinType& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
      
      String name;
    };
    
    class BuiltinValue : public Term {
    public:
      static const TermVtable vtable;
      
      BuiltinValue(CompileContext& compile_context, const SourceLocation& location);
      BuiltinValue(const String& constructor, const String& data, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      String constructor;
      String data;
    };
    
    /**
     * Base type for trees representing functions which are external to user code.
     * 
     * All such functions are entirely non-polymorphic.
     */
    class ExternalFunction : public Term {
      static TreePtr<Term> get_type(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

    public:
      static const SIVtable vtable;
      
      ExternalFunction(const TermVtable *vptr, CompileContext& compile_context, const SourceLocation& location);
      ExternalFunction(const TermVtable *vptr, const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
      static TreePtr<> interface_search_impl(ExternalFunction& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
    };
    
    /**
     * Tree for builtin operations. This saves having to create a separate tree for each one.
     */
    class BuiltinFunction : public ExternalFunction {
      static const TermVtable vtable;
      
    public:
      BuiltinFunction(CompileContext& compile_context, const SourceLocation& location);
      BuiltinFunction(const String& name, const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      String name;
    };

    /**
     * Tree for function calls to C.
     */
    class CFunction : public ExternalFunction {
    public:
      static const TermVtable vtable;
      
      CFunction(CompileContext& compile_context, const SourceLocation& location);
      CFunction(const String& name, const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      String name;
    };
  }
}

#endif
