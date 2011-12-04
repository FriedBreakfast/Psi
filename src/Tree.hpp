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
      /// Parameter depth (number of enclosing parameter scopes between this parameter and its own scope).
      unsigned m_depth;
      /// Index of this parameter in its scope.
      unsigned m_index;
      
    public:
      static const TermVtable vtable;

      Parameter(const TreePtr<Term>& type, unsigned depth, unsigned index, const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);

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
      TreePtr<> m_value;
      TreePtr<Interface> m_interface;
      PSI_STD::vector<TreePtr<Term> > m_wildcard_types;
      PSI_STD::vector<TreePtr<Term> > m_interface_parameters;

      bool matches(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);

    public:
      static const TreeVtable vtable;

      Implementation(CompileContext& compile_context,
                     const TreePtr<>& value,
                     const TreePtr<Interface>& interface,
                     const PSI_STD::vector<TreePtr<Term> >& wildcard_types,
                     const PSI_STD::vector<TreePtr<Term> >& interface_parameters,
                     const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      class PtrHook : public Tree::PtrHook {
        Implementation* get() const {return ptr_as<Implementation>();}

      public:
        const TreePtr<Interface>& interface() const {return get()->m_interface;}
        const TreePtr<>& value() const {return get()->m_value;}
        /// \brief Pattern match variable types.
        const PSI_STD::vector<TreePtr<Term> > wildcard_types() const {return get()->m_wildcard_types;}
        /// \brief Parameters to the interface type
        const PSI_STD::vector<TreePtr<Term> > interface_parameters() const {return get()->m_interface_parameters;}
        bool matches(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters) const {return get()->matches(interface, parameters);}
      };
    };

    class Global : public Term {
      friend class CompileContext;
      void *m_jit_ptr;
    public:
      static const SIVtable vtable;
      Global(const TreePtr<Term>& type, const SourceLocation& location);
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
      TreePtr<Term> m_value;
      
    public:
      static const TermVtable vtable;
      
      Statement(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
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
      /// \brief Single member of this type.
      TreePtr<Term> m_member;
      /// \brief
      PSI_STD::vector<TreePtr<Parameter> > m_parameters;
      /// \brief Implementations carried by this type.
      PSI_STD::vector<TreePtr<Implementation> > m_implementations;

    public:
      static const TreeVtable vtable;

      GenericType(const TreePtr<Term>& member,
                  const PSI_STD::vector<TreePtr<Parameter> >& parameters,
                  const PSI_STD::vector<TreePtr<Implementation> >& implementations,
                  const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);

      class PtrHook : public Tree::PtrHook {
        GenericType* get() const {return ptr_as<GenericType>();}

      public:
        const TreePtr<Term>& member() const {return get()->m_member;}
        const PSI_STD::vector<TreePtr<Parameter> >& parameters() const {return get()->m_parameters;}
        const PSI_STD::vector<TreePtr<Implementation> >& implementations() const {return get()->m_implementations;}
      };
    };

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Term {
      TreePtr<GenericType> m_generic_type;
      /// \brief Arguments to parameters in RecursiveType
      PSI_STD::vector<TreePtr<Term> > m_parameter_values;
      
    public:
      static const TermVtable vtable;

      TypeInstance(const TreePtr<GenericType>& generic_type,
                   const PSI_STD::vector<TreePtr<Term> >& parameter_values,
                   const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);
      static TreePtr<> interface_search_impl(TypeInstance& self, const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters);
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
      TreePtr<Term> m_result_type;
      PSI_STD::vector<TreePtr<Term> > m_argument_types;
      
    public:
      static const TermVtable vtable;

      FunctionType(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

      TreePtr<Term> argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments);
      TreePtr<Term> result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments);
      
      class PtrHook : public Type::PtrHook {
        FunctionType *get() const {return ptr_as<FunctionType>();}
      public:
        const PSI_STD::vector<TreePtr<Term> >& argument_types() const {return get()->m_argument_types;}
        const TreePtr<Term>& result_type() const {return get()->m_result_type;}
        TreePtr<Term> argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const {return get()->argument_type_after(location, arguments);}
        TreePtr<Term> result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const {return get()->result_type_after(location, arguments);}
      };

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<Type>(v);
        v("result_type", &FunctionType::m_result_type)
        ("argument_types", &FunctionType::m_argument_types);
      }
    };

    class Function : public Term {
      PSI_STD::vector<TreePtr<Anonymous> > m_arguments;
      TreePtr<Term> m_result_type;
      TreePtr<Term> m_body;

      static TreePtr<Term> get_type(const TreePtr<Term>& result_type,
                                    const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
                                    const SourceLocation& location);

    public:
      static const TermVtable vtable;
      
      Function(const TreePtr<Term>& result_type,
               const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
               const TreePtr<Term>& body,
               const SourceLocation& location);

      class PtrHook : public Term::PtrHook {
        Function* get() const {return ptr_as<Function>();}
      public:
        const PSI_STD::vector<TreePtr<Anonymous> >& arguments() const {return get()->m_arguments;}
        const TreePtr<Term>& result_type() const {return get()->m_result_type;}
        const TreePtr<Term>& body() const {return get()->m_body;}
      };

      template<typename Visitor> static void visit(Visitor& v);
    };

    class TryFinally : public Term {
      TreePtr<Term> m_try_expr, m_finally_expr;
      
    public:
      static const TermVtable vtable;

      TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
    };
  }
}

#endif
