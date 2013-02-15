#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "StaticDispatch.hpp"
#include "Enums.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief Base for explicit constructor expressions.
     */
    class Constructor : public Functional {
    public:
      static const SIVtable vtable;
      
      Constructor(const TermVtable *vtable, CompileContext& context, const SourceLocation& location);
      Constructor(const TermVtable *vtable, const TreePtr<Term>&, const SourceLocation&);
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Functional>(v);
      }
    };

    /**
     * \brief Base class for constant values.
     * 
     * This is helpful during compilation when one needs to determine
     * whether a value is a simple constant or not.
     */
    class Constant : public Constructor {
    public:
      static const SIVtable vtable;
      
      Constant(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
      Constant(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location);

      template<typename V>
      static void visit(V& v) {
        visit_base<Constructor>(v);
      }
    };
    
    /**
     * \brief Used to indicate that a value should be a global constant.
     */
    class GlobalDefine : public Functional {
    public:
      static const TermVtable vtable;

      GlobalDefine(CompileContext& compile_context, const SourceLocation& location);
      GlobalDefine(const TreePtr<Term>& value, bool functional, const SourceLocation& location);
      
      template<typename V> static void visit(V& v);
      
      TreePtr<Term> value;

      /**
       * Whether the resulting value is expected to be functional.
       * Otherwise, a reference is expected.
       */
      bool functional;
    };

    /**
     * \brief A global value.
     */
    class Global : public Term {
    public:
      static const SIVtable vtable;
      Global(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
      Global(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location);
      
      template<typename V> static void visit(V& v);
      static bool match_impl(const Global& lhs, const Global& rhs, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth);
    };

    /**
     * \brief A global variable or function, which is an element of a Module.
     */
    class ModuleGlobal : public Global {
    public:
      static const SIVtable vtable;
      ModuleGlobal(const VtableType *vptr, CompileContext& compile_context, const SourceLocation& location);
      ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module, PsiBool local, const TreePtr<Term>& type, const SourceLocation& location);
      
      static void global_dependencies_impl(const ModuleGlobal& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);
      template<typename V> static void visit(V& v);
      
      /// \brief Get the module this global should be built into.
      TreePtr<Module> module;
      /// \brief If set, this variable does not have a symbol name.
      PsiBool local;
    };
    
    /**
     * \brief Used to represent globals in modules not being compiled.
     * 
     * This removes the requirement for specifying a value or function body; only a type need be specified.
     */
    class ExternalGlobal : public ModuleGlobal {
    public:
      static const TermVtable vtable;
      ExternalGlobal(CompileContext& compile_context, const SourceLocation& location);
      ExternalGlobal(const TreePtr<Module>& module, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };
    
    /**
     * \brief A global variable.
     */
    class GlobalVariable : public ModuleGlobal {
    public:
      static const TermVtable vtable;
      GlobalVariable(CompileContext& context, const SourceLocation& location);
      GlobalVariable(const TreePtr<Module>& module, PsiBool local, const TreePtr<Term>& value, PsiBool constant, PsiBool merge, const SourceLocation& location);
      
      /// \brief Global variable value.
      TreePtr<Term> value;
      /// \brief Whether the contents of this variable are constant
      PsiBool constant;
      /// \brief Whether address of this variable may alias another variable.
      PsiBool merge;
      
      template<typename V> static void visit(V& v);
    };
    
    /**
     * Wrapper around entries in \c StatementList.
     * 
     * \todo This could easily be removed and \c StatementRef replaced by a reference
     * to the containing block plus an index to the required statement.
     */
    class Statement : public Tree {
    public:
      static const TreeVtable vtable;

      TreePtr<Term> value;
      /// \brief How to store the result of this statement.
      StatementMode mode;
      
      Statement(CompileContext& compile_context, const SourceLocation& location);
      Statement(const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location);
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
      static bool match_impl(const StatementRef& lhs, const StatementRef& rhs, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth);
      static TreePtr<Term> anonymize_impl(const StatementRef& self, const SourceLocation& location,
                                          PSI_STD::vector<TreePtr<Term> >& parameter_types, PSI_STD::map<TreePtr<Statement>, unsigned>& parameter_map,
                                          const PSI_STD::vector<TreePtr<Statement> >& statements, unsigned depth);
    };
    
    /**
     * \brief Tree for a block of code.
     */
    class Block : public Term {
    public:
      static const TermVtable vtable;

      Block(CompileContext& compile_context, const SourceLocation& location);
      Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static void global_dependencies_impl(const Block& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);

      PSI_STD::vector<TreePtr<Statement> > statements;
      TreePtr<Term> value;
      
      static TreePtr<Term> make(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& values, const TreePtr<Term>& result=TreePtr<Term>());
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

      enum GenericTypePrimitive {
        primitive_recurse=0, ///< Primitive if the evaluated member_type is primitive
        primitive_never=1, ///< Never primitive
        primitive_always=2, ///< Always primitive
      };

      GenericType(const PSI_STD::vector<TreePtr<Term> >& pattern,
                  const TreePtr<Term>& member_type,
                  const PSI_STD::vector<TreePtr<OverloadValue> >& overloads,
                  GenericTypePrimitive primitive_mode,
                  const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);

      /// \brief Parameters pattern.
      PSI_STD::vector<TreePtr<Term> > pattern;
      /// \brief Single member of this type.
      TreePtr<Term> member_type;
      /// \brief Overloads carried by this type.
      PSI_STD::vector<TreePtr<OverloadValue> > overloads;
      
      /// \brief Primitive mode: whether or not this type is primitive
      int primitive_mode;
    };

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Functional {
    public:
      static const TermVtable vtable;
      static const bool match_visit = false;

      TypeInstance(CompileContext& compile_context, const SourceLocation& location);
      TypeInstance(const TreePtr<GenericType>& generic,
                   const PSI_STD::vector<TreePtr<Term> >& parameters,
                   const SourceLocation& location);
      
      TreePtr<Term> unwrap() const;

      /// \brief Generic type this is an instance of.
      TreePtr<GenericType> generic;
      /// \brief Arguments to parameters in RecursiveType
      PSI_STD::vector<TreePtr<Term> > parameters;

      template<typename Visitor> static void visit(Visitor& v);
      static bool match_impl(const TypeInstance& lhs, const TypeInstance& rhs, PSI_STD::vector<TreePtr<Term> >& wildcards, unsigned depth);
    };

    /**
     * \brief Value of type RecursiveType.
     */
    class TypeInstanceValue : public Constructor {
    public:
      static const TermVtable vtable;

      TypeInstanceValue(CompileContext& compile_context, const SourceLocation& location);
      TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<Term> member_value;
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
     * \brief Constant type.
     * 
     * Objects of this type can only take on a single value, hence the type
     * is known as "constant". This allows values of this type to be used to
     * fill in for "exists" quantified variables.
     */
    class ConstantType : public Type {
    public:
      static const TermVtable vtable;
      
      ConstantType(CompileContext& compile_context, const SourceLocation& location);
      ConstantType(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Constant value
      TreePtr<Term> value;
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
    class DefaultValue : public Constructor {
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
      PointerType(const TreePtr<Term>& target_type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Get the type referenced by this pointer type.
      TreePtr<Term> target_type;
    };
    
    /**
     * \brief Convert a reference to a pointer
     */
    class PointerTo : public Functional {
    public:
      static const TermVtable vtable;
      PointerTo(CompileContext& compile_context, const SourceLocation& location);
      PointerTo(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief A reference value.
      TreePtr<Term> value;
    };
    
    /**
     * \brief Convert a pointer to a reference.
     */
    class PointerTarget : public Functional {
    public:
      static const TermVtable vtable;
      PointerTarget(CompileContext& compile_context, const SourceLocation& location);
      PointerTarget(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief A pointer value.
      TreePtr<Term> value;
    };
    
    /**
     * \brief Cast a pointer to a different type.
     * 
     * This is not like C++ pointer casting, no pointer adjustment is ever performed.
     */
    class PointerCast : public Functional {
    public:
      static const TermVtable vtable;
      PointerCast(CompileContext& compile_context, const SourceLocation& location);
      PointerCast(const TreePtr<Term>& value, const TreePtr<Term>& target_type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief A pointer value.
      TreePtr<Term> value;
      /// \brief Cast target type
      TreePtr<Term> target_type;
    };
    
    /**
     * \brief Get a pointer to an element from a pointer to a value.
     */
    class ElementPtr : public Functional {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      ElementPtr(CompileContext& compile_context, const SourceLocation& location);
      ElementPtr(const TreePtr<Term>& value, const TreePtr<Term>& index, const SourceLocation& location);
      ElementPtr(const TreePtr<Term>& value, int index, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Value of pointer to aggregate.
      TreePtr<Term> value;
      /// \brief Index of member to get.
      TreePtr<Term> index;
    };
    
    /**
     * \brief Get a reference to a member from a reference to a value.
     */
    class ElementValue : public Functional {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      ElementValue(CompileContext& compile_context, const SourceLocation& location);
      ElementValue(const TreePtr<Term>& value, const TreePtr<Term>& index, const SourceLocation& location);
      ElementValue(const TreePtr<Term>& value, int index, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Value of aggregate.
      TreePtr<Term> value;
      /// \brief Index of member to get.
      TreePtr<Term> index;
    };
    
    /**
     * \brief Get a reference to a containing structure from a reference to an inner value.
     */
    class OuterPtr : public Functional {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      OuterPtr(CompileContext& compile_context, const SourceLocation& location);
      OuterPtr(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Pointer to data structure, which must have type DerivedType
      TreePtr<Term> value;
    };
    
    /**
     * \brief Get a reference to a containing structure from a reference to an inner value.
     */
    class OuterValue : public Functional {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      OuterValue(CompileContext& compile_context, const SourceLocation& location);
      OuterValue(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Pointer to data structure, which must have type DerivedType
      TreePtr<Term> value;
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
    class StructValue : public Constructor {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      
      StructValue(CompileContext& compile_context, const SourceLocation& location);
      StructValue(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);
      StructValue(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      PSI_STD::vector<TreePtr<Term> > members;
    };
    
    /**
     * \brief Array type.
     */
    class ArrayType : public Type {
    public:
      static const TermVtable vtable;
      ArrayType(CompileContext& compile_context, const SourceLocation& location);
      ArrayType(const TreePtr<Term>& element_type, const TreePtr<Term>& length, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      TreePtr<Term> element_type;
      TreePtr<Term> length;
    };
    
    /**
     * \brief Array value.
     */
    class ArrayValue : public Constructor {
    public:
      static const TermVtable vtable;
      ArrayValue(CompileContext& compile_context, const SourceLocation& location);
      ArrayValue(const TreePtr<ArrayType>& type, const PSI_STD::vector<TreePtr<Term> >& element_values, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      PSI_STD::vector<TreePtr<Term> > element_values;
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
     * \brief Type of upward references.
     */
    class UpwardReferenceType : public Type {
    public:
      static const TermVtable vtable;
      UpwardReferenceType(CompileContext& compile_context, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };
    
    /**
     * \brief Value type of upward references, \c UpwardReferenceType.
     */
    class UpwardReference : public Constructor {
    public:
      static const TermVtable vtable;
      UpwardReference(CompileContext& context, const SourceLocation& location);
      UpwardReference(const TreePtr<Term>& outer_type, const TreePtr<Term>& outer_index, const TreePtr<Term>& next, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Type of outer data structure.
      TreePtr<Term> outer_type;
      /// \brief Index of pointer in outer data structure.
      TreePtr<Term> outer_index;
      /// \brief Next upward reference in the chain
      TreePtr<Term> next;
    };
    
    /**
     * \brief Associates upward reference information with a type.
     * 
     * This maps onto the functionality provided by the second parameter to
     * TVM's pointer type, but has to be a separate class here because references
     * are pointers with implicit behaviour and cannot have an extra parameter
     * added.
     */
    class DerivedType : public Type {
    public:
      static const TermVtable vtable;
      DerivedType(CompileContext& compile_context, const SourceLocation& location);
      DerivedType(const TreePtr<Term>& value_type, const TreePtr<Term>& upref, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Type of the value associated with this inner pointer/reference.
      TreePtr<Term> value_type;
      /// \brief Upward reference information.
      TreePtr<Term> upref;
    };
    
    class InterfaceValue;
    
    struct FunctionParameterType {
      ParameterMode mode;
      TreePtr<Term> type;
      
      FunctionParameterType() {}
      FunctionParameterType(ParameterMode mode_, const TreePtr<Term>& type_) : mode(mode_), type(type_) {}
      
      template<typename V>
      static void visit(V& v) {
        v("mode", &FunctionParameterType::mode)
        ("type", &FunctionParameterType::type);
      }
    };
    
    /**
     * \brief Implements exists quantification.
     */
    class Exists : public Type {
    public:
      static const TermVtable vtable;
      static const bool match_parameterized = true;

      Exists(CompileContext& compile_context, const SourceLocation& location);
      Exists(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& parameter_types, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      TreePtr<Term> parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      
      TreePtr<Term> result;
      PSI_STD::vector<TreePtr<Term> > parameter_types;
    };
    
    class FunctionType : public Type {
    public:
      static const TermVtable vtable;
      static const bool match_parameterized = true;

      FunctionType(CompileContext& compile_context, const SourceLocation& location);
      FunctionType(ResultMode result_mode, const TreePtr<Term>& result_type,
                   const PSI_STD::vector<FunctionParameterType>& parameter_types,
                   const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces, const SourceLocation& location);
      template<typename V> static void visit(V& v);

      TreePtr<Term> parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      
      ResultMode result_mode;
      TreePtr<Term> result_type;
      PSI_STD::vector<FunctionParameterType> parameter_types;
      PSI_STD::vector<TreePtr<InterfaceValue> > interfaces;
    };
    
    class JumpTarget;

    class Function : public ModuleGlobal {
    public:
      static const TermVtable vtable;
      
      Function(CompileContext& compile_context, const SourceLocation& location);
      Function(const TreePtr<Module>& module,
               bool local,
               const TreePtr<FunctionType>& type,
               const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
               const TreePtr<Term>& body,
               const TreePtr<JumpTarget>& return_target,
               const SourceLocation& location);

      /// \brief Argument values.
      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      /// \brief Function body.
      TreePtr<Term> body;
      /**
       * \brief Target to jump to in order to return from the function.
       * 
       * This may be NULL if the function only exits ordinarily.
       */
      TreePtr<JumpTarget> return_target;

      template<typename Visitor> static void visit(Visitor& v);
      static void global_dependencies_impl(const Function& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);
    };

    /**
     * \brief Function inside another function.
     * 
     * This term exists to allow the compiler to figure out which variables are required by a closure and then
     * automatically generate a data structure containing them.
     */
    class Closure : public Term {
    public:
      static const TermVtable vtable;
      
      Closure(const TreePtr<FunctionType>& type,
              const TreePtr<Anonymous>& closure_data_type,
              const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
              const TreePtr<Term>& closure_data,
              const TreePtr<Term>& body,
              const TreePtr<JumpTarget>& return_target,
              const SourceLocation& location);
      
      template<typename V> static void visit(V& v);
      static void global_dependencies_impl(const Closure& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);
      
      /**
       * \brief Closure data type.
       * 
       * This anonymous tree must have type Metatype.
       * During function compilation it will be replaced by the actual type.
       */
      TreePtr<Anonymous> closure_data_type;
      
      /**
       * \brief Argument values.
       * 
       * Note that somewhere amongst these arguments the closure data is required to
       * be present, so that \c closure_data may access it.
       */
      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      
      /**
       * \brief How to get the closure data inside the function.
       * 
       * This is used to allow the user to specify how the generated function
       * should access the automatically generated closure data, rather than
       * forcing it to be the first parameter, for instance, although this is
       * usually what it will be.
       * 
       * This term may not use variables captured from outer function scopes.
       */
      TreePtr<Term> closure_data;
      
      /**
       * \brief Function body.
       * 
       * This term may use variables in outer function scopes.
       */
      TreePtr<Term> body;
      
      /// \copydoc Function::return_target
      TreePtr<JumpTarget> return_target;
    };

    /**
     * \brief Try-finally.
     * 
     * Executes try expression, then evaluate finally expression.
     * If except_only is true, the finally expression is only run
     * for exceptional exits.
     */
    class TryFinally : public Term {
    public:
      static const TermVtable vtable;

      TryFinally(CompileContext& compile_context, const SourceLocation& location);
      TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, bool except_only, const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);
      static void global_dependencies_impl(const TryFinally& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);

      TreePtr<Term> try_expr, finally_expr;
      /// Only run finally_expr in exceptional exits.
      bool except_only;
    };
    
    /**
     * \brief If-Then-Else.
     */
    class IfThenElse : public Term {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      
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
      JumpTarget(CompileContext& compile_context, const TreePtr<Term>& value, ResultMode argument_mode, const TreePtr<Anonymous>& argument, const SourceLocation& location);
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
      static void global_dependencies_impl(const JumpGroup& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);
      
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
      static void global_dependencies_impl(const JumpTo& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);
      
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
      static void global_dependencies_impl(const FunctionCall& self, PSI_STD::set<TreePtr<ModuleGlobal> >& globals);

      TreePtr<Term> target;
      PSI_STD::vector<TreePtr<Term> > arguments;
      
      TreePtr<FunctionType> target_type();
    };
    
    /**
     * \brief Provide a value for a phantom term during evaluation of a tree.
     */
    class SolidifyDuring : public Term {
    public:
      static const TermVtable vtable;
      
      SolidifyDuring(CompileContext& context, const SourceLocation& location);
      SolidifyDuring(const TreePtr<Term>& value, const TreePtr<Term>& body, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Value being supplied; must have type ConstantType
      TreePtr<Term> value;
      /// \brief Tree to be evaluated.
      TreePtr<Term> body;
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
      static const bool match_visit = true;
      
      PrimitiveType(CompileContext& compile_context, const SourceLocation& location);
      PrimitiveType(CompileContext& compile_context, const String& name, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      String name;
    };
    
    /**
     * \brief Tree for built in values.
     */
    class BuiltinValue : public Constant {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      
      BuiltinValue(CompileContext& compile_context, const SourceLocation& location);
      BuiltinValue(const String& constructor, const String& data, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);

      String constructor;
      String data;
    };
    
    /**
     * \brief Class for small integer values.
     * 
     * Holds a 32 bit signed integer value.
     * Note that it is a requirement to use this type of constant to index
     * structures and unions.
     */
    class IntegerValue : public Constant {
    public:
      static const TermVtable vtable;
      IntegerValue(CompileContext& compile_context, const SourceLocation& location);
      IntegerValue(const TreePtr<Term>& type, int value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      int value;
    };
    
    /**
     * \brief Class for string data.
     */
    class StringValue : public Constant {
    public:
      static const TermVtable vtable;
      StringValue(CompileContext& compile_context, const SourceLocation& location);
      StringValue(CompileContext& compile_context, const String& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      String value;

      static TreePtr<Term> string_element_type(CompileContext& compile_context, const SourceLocation& location);
      static TreePtr<Term> string_type(CompileContext& compile_context, const TreePtr<Term>& length, const SourceLocation& location);
      static TreePtr<Term> string_type(CompileContext& compile_context, unsigned length, const SourceLocation& location);
    };
    
    /**
     * Tree for builtin operations. This saves having to create a separate tree for each one.
     */
    class BuiltinFunction : public Global {
    public:
      static const TermVtable vtable;
      static const bool match_visit = true;
      
      BuiltinFunction(CompileContext& compile_context, const SourceLocation& location);
      BuiltinFunction(const String& name, bool pure, const TreePtr<FunctionType>& type, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      /**
       * \brief Builtin identifier.
       */
      String name;
      
      /**
       * \brief Whether this function dependends on any external state.
       * 
       * If this is true, then all parameters to this function must be passed in functional mode,
       * and the result of this function may only depend on the values of those parameters.
       */
      PsiBool pure;
    };
    
    class TargetCallback;
    
    struct TargetCallbackVtable {
      TreeVtable base;
      void (*evaluate) (PropertyValue *result, const TargetCallback *self, const PropertyValue *local, const PropertyValue *cross);
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
      PropertyValue evaluate(const PropertyValue& target, const PropertyValue& local) const {
        ResultStorage<PropertyValue> result;
        derived_vptr(this)->evaluate(result.ptr(), this, &target, &local);
        return result.done();
      }
    };
    
    template<typename Derived, typename Impl=Derived>
    struct TargetCallbackWrapper {
      static void evaluate(PropertyValue *result, const TargetCallback *self, const PropertyValue *local, const PropertyValue *cross) {
        new (result) PropertyValue (Impl::evaluate_impl(*static_cast<const Derived*>(self), *local, *cross));
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
    class LibrarySymbol : public Global {
    public:
      static const TermVtable vtable;

      LibrarySymbol(CompileContext& compile_context, const SourceLocation& location);
      LibrarySymbol(const TreePtr<Library>& library, const TreePtr<TargetCallback>& callback, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Library this symbol is in.
      TreePtr<Library> library;
      /// \brief Callback to get the symbol name
      TreePtr<TargetCallback> callback;
    };
    
    /**
     * \brief Namespace data.
     */
    class Namespace : public Tree {
    public:
      static const TreeVtable vtable;
      
      typedef PSI_STD::map<String, TreePtr<Term> > NameMapType;

      Namespace(CompileContext& compile_context, const SourceLocation& location);
      Namespace(CompileContext& compile_context, const NameMapType& members, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      NameMapType members;
    };
    
    /**
     * \brief Get the value associated with an interface for particular parameters.
     */
    class InterfaceValue : public Term {
    public:
      static const TermVtable vtable;
      
      InterfaceValue(CompileContext& context, const SourceLocation& location);
      InterfaceValue(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Interface.
      TreePtr<Interface> interface;
      /// \brief List of parameters, including implicit parameters.
      PSI_STD::vector<TreePtr<Term> > parameters;
    };
  }
}

#endif
