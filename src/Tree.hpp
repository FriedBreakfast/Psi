#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "StaticDispatch.hpp"

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
      template<typename V> static void visit(V& v);
    };
    
    /**
     * \brief A global variables, which is an element of a Module.
     */
    class Global : public Term {
    public:
      static const SIVtable vtable;
      Global(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
      Global(const VtableType *vptr, const TreePtr<Module>& module, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Get the module this global should be built into.
      TreePtr<Module> module;
    };
    
    class ExternalGlobal : public Global {
    public:
      static const TermVtable vtable;
      ExternalGlobal(CompileContext& compile_context, const SourceLocation& location);
      ExternalGlobal(const TreePtr<Module>& module, const TreePtr<Term>& type, const String& symbol, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      String symbol;
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
    };
    
    /**
     * \brief Reference to value generated by a Statement.
     * 
     * This allows references to Statement to have a separate location from
     * the original statement.
     */
    class StatementRef : public Term {
    public:
      static const TermVtable vtable;
      
      TreePtr<Statement> value;

      StatementRef(CompileContext& compile_context, const SourceLocation& location);
      StatementRef(const TreePtr<Statement>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
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
      template<typename V> static void visit(V& v);
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

      GenericType(const PSI_STD::vector<TreePtr<Term> >& pattern,
                  const TreePtr<Term>& member_type,
                  const PSI_STD::vector<TreePtr<OverloadValue> >& overloads,
                  const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);

      /// \brief Parameters pattern.
      PSI_STD::vector<TreePtr<Term> > pattern;
      /// \brief Single member of this type.
      TreePtr<Term> member_type;
      /// \brief Overloads carried by this type.
      PSI_STD::vector<TreePtr<OverloadValue> > overloads;
    };

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Term {
    public:
      static const TermVtable vtable;

      TypeInstance(CompileContext& compile_context, const SourceLocation& location);
      TypeInstance(const TreePtr<GenericType>& generic,
                   const PSI_STD::vector<TreePtr<Term> >& parameters,
                   const SourceLocation& location);

      /// \brief Generic type this is an instance of.
      TreePtr<GenericType> generic;
      /// \brief Arguments to parameters in RecursiveType
      PSI_STD::vector<TreePtr<Term> > parameters;

      template<typename Visitor> static void visit(Visitor& v);
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
      template<typename V> static void visit(V& v);
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
      template<typename V> static void visit(V& v);
    };

    /**
     * \brief Default initialized value.
     */
    class DefaultValue : public Term {
    public:
      static const TermVtable vtable;
      DefaultValue(CompileContext& compile_context, const SourceLocation& location);
      DefaultValue(const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };
    
    /**
     * \brief Pointer type.
     */
    class PointerType : public Type {
    public:
      static const TermVtable vtable;
      PointerType(CompileContext& compile_context, const SourceLocation& location);
      PointerType(CompileContext& compile_context, const TreePtr<Term>& target_type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Get the type referenced by this pointer type.
      TreePtr<Term> target_type;
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
      template<typename V> static void visit(V& v);

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
      template<typename V> static void visit(V& v);

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
      template<typename V> static void visit(V& v);

      PSI_STD::vector<TreePtr<Term> > members;
    };
    
    /**
     * \brief Storage modes for function parameters.
     * 
     * \see \ref storage_specifiers
     */
    enum ParameterMode {
      parameter_mode_input, ///< Input parameter
      parameter_mode_output, ///< Output parameter
      parameter_mode_io, ///< Input/Output parameter
      parameter_mode_rvalue, ///< R-value reference
      parameter_mode_functional ///< Funtional value
    };
    
    PSI_VISIT_SIMPLE(ParameterMode);
    
    /**
     * \brief Storage modes for function return values and jump parameters.
     * *
     * \see \ref storage_specifiers
     */
    enum ResultMode {
      result_mode_by_value, ///< By value
      result_mode_functional, ///< By value, functional
      result_mode_rvalue, ///< R-value reference
      result_mode_lvalue ///< L-value reference
    };
    
    PSI_VISIT_SIMPLE(ResultMode);
    
    struct FunctionParameterType {
      ParameterMode mode;
      TreePtr<Term> type;
      
      template<typename V>
      static void visit(V& v) {
        v("mode", &FunctionParameterType::mode)
        ("type", &FunctionParameterType::type);
      }
    };

    class FunctionType : public Type {
    public:
      static const TermVtable vtable;

      FunctionType(CompileContext& compile_context, const SourceLocation& location);
      FunctionType(ResultMode result_mode, const TreePtr<Term>& result_type, const std::vector<std::pair<ParameterMode, TreePtr<Anonymous> > >& arguments, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      TreePtr<Term> argument_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_type_after(const SourceLocation& location, const List<TreePtr<Term> >& arguments) const;
      
      ResultMode result_mode;
      TreePtr<Term> result_type;
      PSI_STD::vector<FunctionParameterType> argument_types;
    };
    
    class JumpTarget;

    class Function : public Global {
    public:
      static const TermVtable vtable;
      
      Function(CompileContext& compile_context, const SourceLocation& location);
      Function(const TreePtr<Module>& module,
               ResultMode result_mode,
               const TreePtr<Term>& result_type,
               const std::vector<std::pair<ParameterMode, TreePtr<Anonymous> > >& arguments,
               const TreePtr<Term>& body,
               const SourceLocation& location);

      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      TreePtr<Term> result_type;
      TreePtr<Term> body;
      /// \brief Target to jump to in order to return from the function.
      TreePtr<JumpTarget> return_target;

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
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<Term> condition;
      TreePtr<Term> true_value;
      TreePtr<Term> false_value;
    };
    
    /**
     * \brief Jump target.
     * 
     * Target of JumpTo instruction.
     */
    class JumpTarget : public Tree {
    public:
      static const TreeVtable vtable;
      
      JumpTarget(CompileContext& compile_context, const SourceLocation& location);
      JumpTarget(const TreePtr<Term>& value, ResultMode argument_mode, const TreePtr<Anonymous>& argument, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<Term> value;
      ResultMode argument_mode;
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
      JumpGroup(const TreePtr<Term>& initial, const PSI_STD::vector<TreePtr<JumpTarget> >& values, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<Term> initial;
      PSI_STD::vector<TreePtr<JumpTarget> > entries;
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
      JumpTo(const TreePtr<JumpTarget>& target, const TreePtr<Term>& argument, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<JumpTarget> target;
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
      
      TreePtr<FunctionType> target_type();
    };
    
    /**
     * Tree for builtin types.
     * 
     * This saves having to create a separate tree for each one, at least until later in the compilation process
     * so that a uniform syntax may be used by the user.
     */
    class PrimitiveType : public Type {
    public:
      static const TermVtable vtable;
      
      PrimitiveType(CompileContext& compile_context, const SourceLocation& location);
      PrimitiveType(CompileContext& compile_context, const String& name, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

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
      template<typename Visitor> static void visit(Visitor& v);
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
    
    class TargetCallback;
    
    struct TargetCallbackVtable {
      TreeVtable base;
      void (*evaluate) (PropertyValue *result, TargetCallback *self, const PropertyValue *local, const PropertyValue *cross);
    };
    
    class TargetCallback : public Tree {
    public:
      typedef TargetCallbackVtable VtableType;
      static const SIVtable vtable;
      
      TargetCallback(const TargetCallbackVtable *vtable, CompileContext& compile_context, const SourceLocation& location);
      
      /**
       * \brief Evaluate a target-depenent value.
       * 
       * \param target Target description of system being compiler for.
       * \param local Target description of system being compiled on.
       */
      PropertyValue evaluate(const PropertyValue& target, const PropertyValue& local) {
        ResultStorage<PropertyValue> result;
        derived_vptr(this)->evaluate(result.ptr(), this, &target, &local);
        return result.done();
      }
    };
    
    template<typename Derived, typename Impl=Derived>
    struct TargetCallbackWrapper {
      static void evaluate(PropertyValue *result, TargetCallback *self, const PropertyValue *local, const PropertyValue *cross) {
        new (result) PropertyValue (Impl::evaluate_impl(*static_cast<Derived*>(self), *local, *cross));
      }
    };
    
#define PSI_COMPILER_TARGET_CALLBACK_VTABLE(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &::Psi::Compiler::TargetCallbackWrapper<derived>::evaluate \
  }

    /**
     * \brief A library.
     * 
     * This is a target-dependent tree. The compilation target must be
     * able to interpret the result of the callback.
     */
    class Library : public Tree {
    public:
      static const TreeVtable vtable;
      
      Library(CompileContext& compile_context, const SourceLocation& location);
      Library(const TreePtr<TargetCallback>& callback, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Callback to get details of the library.
      TreePtr<TargetCallback> callback;
    };
    
    /**
     * \brief Symbol imported from a library.
     */
    class LibrarySymbol : public Term {
    public:
      static const TermVtable vtable;

      LibrarySymbol(CompileContext& compile_context, const SourceLocation& location);
      LibrarySymbol(const TreePtr<Library>& library, const TreePtr<TargetCallback>& callback, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Library this symbol is in.
      TreePtr<Library> library;
      /// \brief Callback to get the symbol name
      TreePtr<TargetCallback> callback;
    };
  }
}

#endif
