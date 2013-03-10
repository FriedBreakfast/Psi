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
      
      Constructor(const VtableType *vtable);
      
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
      
      Constant(const VtableType *vptr);

      template<typename V>
      static void visit(V& v) {
        visit_base<Constructor>(v);
      }
    };
    
    /**
     * \brief A global value.
     */
    class Global : public Term {
    public:
      static const SIVtable vtable;
      Global(const VtableType *vptr, const TreePtr<Term>& type, const SourceLocation& location);
      Global(const VtableType *vptr, const TermResultInfo& type, const SourceLocation& location);
      
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const Global& self);
    };

    /**
     * \brief A global variable or function, which is an element of a Module.
     */
    class ModuleGlobal : public Global {
    public:
      static const SIVtable vtable;
      ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module, const TreePtr<Term>& type, PsiBool local, const SourceLocation& location);
      ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module, const TermResultInfo& type, PsiBool local, const SourceLocation& location);
      
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
      static const VtableType vtable;
      ExternalGlobal(const TreePtr<Module>& module, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };
    
    /**
     * \brief Equivalent to a statement, but as part of a Module rather than a function.
     */
    class GlobalStatement : public ModuleGlobal {
    public:
      static const VtableType vtable;

      TreePtr<Term> value;
      /// \brief How to store the result of this statement.
      StatementMode mode;
      
      GlobalStatement(const TreePtr<Module>& module, const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static TermTypeInfo type_info_impl(const GlobalStatement& self);
    };

    /**
     * \brief A global variable.
     */
    class GlobalVariable : public ModuleGlobal {
      DelayedValue<TreePtr<Term>, TreePtr<GlobalVariable> > m_value;
      TreePtr<GlobalVariable> get_ptr() const {return tree_from(this);}
      
    public:
      static const VtableType vtable;

      template<typename ValueCallback>
      GlobalVariable(const TreePtr<Module>& module, const TreePtr<Term>& type, bool local, bool constant_, bool merge_,
                     const SourceLocation& location, const ValueCallback& value)
      : ModuleGlobal(&vtable, module, type, local, location),
      m_value(module.compile_context(), location, value),
      constant(constant_),
      merge(merge_) {
      }
      
      /// \brief Whether the contents of this variable are constant
      PsiBool constant;
      /// \brief Whether address of this variable may alias another variable.
      PsiBool merge;
      
      /// \brief Global variable value.
      const TreePtr<Term>& value() const {
        return m_value.get(this, &GlobalVariable::get_ptr);
      }
      
      template<typename V> static void visit(V& v);
      static void local_complete_impl(const GlobalVariable& self);
    };
    
    /**
     * \brief Governs evaluation location of inner terms.
     * 
     * This also allows cross references in the tree, since it is assumed a Statement
     * term is evaluated as part of a Block, and any other uses should re-use the existing
     * value.
     */
    class Statement : public Term {
    public:
      static const VtableType vtable;

      TreePtr<Term> value;
      /// \brief How to store the result of this statement.
      StatementMode mode;
      
      Statement(const TreePtr<Term>& value, StatementMode mode, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static TermTypeInfo type_info_impl(const Statement& self);
    };
    
    /**
     * \brief Tree for a block of code.
     */
    class Block : public Term {
    public:
      static const VtableType vtable;

      Block(const PSI_STD::vector<TreePtr<Statement> >& statements, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static TermTypeInfo type_info_impl(const Block& self);

      PSI_STD::vector<TreePtr<Statement> > statements;
      TreePtr<Term> value;
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
      DelayedValue<TreePtr<Term>, TreePtr<GenericType> > m_member;
      DelayedValue<PSI_STD::vector<TreePtr<OverloadValue> >, TreePtr<GenericType> > m_overloads;
      TreePtr<GenericType> get_ptr() const {return tree_from(this);}

    public:
      static const VtableType vtable;

      enum GenericTypePrimitive {
        primitive_recurse=0, ///< Primitive if the evaluated member_type is primitive
        primitive_never=1, ///< Never primitive
        primitive_always=2, ///< Always primitive
      };

      template<typename T, typename U>
      GenericType(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& pattern_,
                  const T& member_callback, const U& overloads_callback,
                  GenericTypePrimitive primitive_mode_, const SourceLocation& location)
      : Tree(&vtable, compile_context, location),
      m_member(compile_context, location, member_callback),
      m_overloads(compile_context, location, overloads_callback),
      pattern(pattern_),
      primitive_mode(primitive_mode_) {
      }

      template<typename Visitor> static void visit(Visitor& v);
      static void local_complete_impl(const GenericType& self);

      /// \brief Parameters pattern.
      PSI_STD::vector<TreePtr<Term> > pattern;
      /// \brief Primitive mode: whether or not this type is primitive
      int primitive_mode;
      
      /// \brief Single member of this type.
      const TreePtr<Term>& member_type() const {
        return m_member.get(this, &GenericType::get_ptr);
      }
      
      /// \brief If the member type is currently being built.
      bool member_running() const {
        return m_member.running();
      }
      
      /// \brief Overloads carried by this type.
      const PSI_STD::vector<TreePtr<OverloadValue> >& overloads() const {
        return m_overloads.get(this, &GenericType::get_ptr);
      }
    };

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Type {
    public:
      static const VtableType vtable;

      TypeInstance(const TreePtr<GenericType>& generic,
                   const PSI_STD::vector<TreePtr<Term> >& parameters);
      
      TreePtr<Term> unwrap() const;

      /// \brief Generic type this is an instance of.
      TreePtr<GenericType> generic;
      /// \brief Arguments to parameters in RecursiveType
      PSI_STD::vector<TreePtr<Term> > parameters;

      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const TypeInstance& self);
      static TermTypeInfo type_info_impl(const TypeInstance& self);
    };

    /**
     * \brief Value of type RecursiveType.
     */
    class TypeInstanceValue : public Constructor {
    public:
      static const VtableType vtable;

      TypeInstanceValue(const TreePtr<TypeInstance>& type, const TreePtr<Term>& member_value);
      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const TypeInstanceValue& self);
      static TermTypeInfo type_info_impl(const TypeInstanceValue& self);
      
      TreePtr<TypeInstance> type_instance;
      TreePtr<Term> member_value;
    };
    
    /**
     * \brief Bottom type.
     * 
     * This type cannot be instantiated, hence any expression of this type
     * cannot return.
     */
    class BottomType : public Type {
    public:
      static const VtableType vtable;
      BottomType();
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const BottomType& self);
      static TermTypeInfo type_info_impl(const BottomType& self);
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
      static const VtableType vtable;
      
      ConstantType(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ConstantType& self);
      static TermTypeInfo type_info_impl(const ConstantType& self);
      
      /// \brief Constant value
      TreePtr<Term> value;
    };

    /**
     * \brief Empty type.
     *
     * Unions and structures with no members can be replaced by this type.
     */
    class EmptyType : public Type {
    public:
      static const VtableType vtable;
      EmptyType();
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const EmptyType& self);
      static TermTypeInfo type_info_impl(const EmptyType& self);
    };

    /**
     * \brief Default initialized value.
     */
    class DefaultValue : public Constructor {
    public:
      static const VtableType vtable;
      DefaultValue(const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const DefaultValue& self);
      static TermTypeInfo type_info_impl(const DefaultValue& self);
      TreePtr<Term> value_type;
    };
    
    /**
     * \brief Pointer type.
     */
    class PointerType : public Type {
    public:
      static const VtableType vtable;
      PointerType(const TreePtr<Term>& target_type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const PointerType& self);
      static TermTypeInfo type_info_impl(const PointerType& self);
      
      /// \brief Get the type referenced by this pointer type.
      TreePtr<Term> target_type;
    };
    
    /**
     * \brief Convert a reference to a pointer
     */
    class PointerTo : public Functional {
    public:
      static const VtableType vtable;
      PointerTo(const TreePtr<Term>& value);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const PointerTo& self);
      static TermTypeInfo type_info_impl(const PointerTo& self);
      
      /// \brief A reference value.
      TreePtr<Term> value;
    };
    
    /**
     * \brief Convert a pointer to a reference.
     */
    class PointerTarget : public Functional {
    public:
      static const VtableType vtable;
      PointerTarget(const TreePtr<Term>& value);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const PointerTarget& self);
      static TermTypeInfo type_info_impl(const PointerTarget& self);
      
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
      static const VtableType vtable;
      PointerCast(const TreePtr<Term>& value, const TreePtr<Term>& target_type);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const PointerCast& self);
      static TermTypeInfo type_info_impl(const PointerCast& self);
      
      /// \brief A pointer value.
      TreePtr<Term> value;
      /// \brief Cast target type
      TreePtr<Term> target_type;
    };
    
    /**
     * \brief Get a reference to a member from a reference to a value.
     */
    class ElementValue : public Functional {
    public:
      static const VtableType vtable;
      ElementValue(const TreePtr<Term>& value, const TreePtr<Term>& index);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ElementValue& self);
      static TermTypeInfo type_info_impl(const ElementValue& self);
      
      /// \brief Value of aggregate.
      TreePtr<Term> value;
      /// \brief Index of member to get.
      TreePtr<Term> index;

      static TreePtr<Term> element_type(const TreePtr<Term>& aggregate, const TreePtr<Term>& index, const SourceLocation& location);
    };
    
    /**
     * \brief Get a reference to a containing structure from a reference to an inner value.
     */
    class OuterValue : public Functional {
    public:
      static const VtableType vtable;
      OuterValue(const TreePtr<Term>& value);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const OuterValue& self);
      static TermTypeInfo type_info_impl(const OuterValue& self);
      
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
      static const VtableType vtable;
      StructType(const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const StructType& self);
      static TermTypeInfo type_info_impl(const StructType& self);

      PSI_STD::vector<TreePtr<Term> > members;
    };

    /**
     * \brief Structure value.
     */
    class StructValue : public Constructor {
    public:
      static const VtableType vtable;
      StructValue(const TreePtr<StructType>& type, const PSI_STD::vector<TreePtr<Term> >& members);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const StructValue& self);
      static TermTypeInfo type_info_impl(const StructValue& self);

      TreePtr<StructType> struct_type;
      PSI_STD::vector<TreePtr<Term> > members;
    };
    
    /**
     * \brief Array type.
     */
    class ArrayType : public Type {
    public:
      static const VtableType vtable;
      ArrayType(const TreePtr<Term>& element_type, const TreePtr<Term>& length, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ArrayType& self);
      static TermTypeInfo type_info_impl(const ArrayType& self);
      
      TreePtr<Term> element_type;
      TreePtr<Term> length;
    };
    
    /**
     * \brief Array value.
     */
    class ArrayValue : public Constructor {
    public:
      static const VtableType vtable;
      ArrayValue(const TreePtr<ArrayType>& type, const PSI_STD::vector<TreePtr<Term> >& element_values);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ArrayValue& self);
      static TermTypeInfo type_info_impl(const ArrayValue& self);
      
      TreePtr<ArrayType> array_type;
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
      static const VtableType vtable;
      UnionType(const PSI_STD::vector<TreePtr<Term> >& members, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const UnionType& self);
      static TermTypeInfo type_info_impl(const UnionType& self);

      PSI_STD::vector<TreePtr<Term> > members;
    };
    
    /**
     * \brief Union value.
     */
    class UnionValue : public Constructor {
    public:
      static const VtableType vtable;
      UnionValue(const TreePtr<UnionType>& type, const TreePtr<Term>& member_value);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const UnionValue& self);
      static TermTypeInfo type_info_impl(const UnionValue& self);
      
      TreePtr<UnionType> union_type;
      TreePtr<Term> member_value;
    };
    
    /**
     * \brief Type of upward references.
     */
    class UpwardReferenceType : public Type {
    public:
      static const VtableType vtable;
      UpwardReferenceType();
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const UpwardReferenceType& self);
      static TermTypeInfo type_info_impl(const UpwardReferenceType& self);
    };
    
    /**
     * \brief Value type of upward references, \c UpwardReferenceType.
     */
    class UpwardReference : public Constructor {
    public:
      static const VtableType vtable;
      UpwardReference(const TreePtr<Term>& outer_type, const TreePtr<Term>& outer_index, const TreePtr<Term>& next, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const UpwardReference& self);
      static TermTypeInfo type_info_impl(const UpwardReference& self);
      static TreePtr<Term> rewrite_impl(const UpwardReference& self, TermRewriter& rewriter, const SourceLocation& location);
      
      /**
       * \brief Type of outer data structure.
       * 
       * This may be NULL if \c next is non-NULL, to avoid certain difficult cases of recursion.
       */
      TreePtr<Term> maybe_outer_type;
      /// \brief Index of pointer in outer data structure.
      TreePtr<Term> outer_index;
      /// \brief Next upward reference in the chain
      TreePtr<Term> next;
      
      TreePtr<Term> inner_type() const;
      TreePtr<Term> outer_type() const;
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
      static const VtableType vtable;
      DerivedType(const TreePtr<Term>& value_type, const TreePtr<Term>& upref, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const DerivedType& self);
      static TermTypeInfo type_info_impl(const DerivedType& self);
      
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
      
      friend std::size_t hash_value(const FunctionParameterType& self) {
        std::size_t h = 0;
        boost::hash_combine(h, self.mode);
        boost::hash_combine(h, self.type);
        return h;
      }
    };
    
    inline bool operator == (const FunctionParameterType& lhs, const FunctionParameterType& rhs) {
      return (lhs.mode == rhs.mode) && (lhs.type == rhs.type);
    }
    
    class ParameterizedType : public Type {
    public:
      static const SIVtable vtable;
      ParameterizedType(const VtableType *vptr);
    };
    
    /**
     * \brief Implements exists quantification.
     */
    class Exists : public ParameterizedType {
    public:
      static const VtableType vtable;

      Exists(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& parameter_types, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const Exists& self);
      static TermTypeInfo type_info_impl(const Exists& self);

      TreePtr<Term> parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      
      TreePtr<Term> result;
      PSI_STD::vector<TreePtr<Term> > parameter_types;
    };
    
    class FunctionType : public ParameterizedType {
    public:
      static const VtableType vtable;

      FunctionType(ResultMode result_mode, const TreePtr<Term>& result_type,
                   const PSI_STD::vector<FunctionParameterType>& parameter_types,
                   const PSI_STD::vector<TreePtr<InterfaceValue> >& interfaces,
                   const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const FunctionType& self);
      static TermTypeInfo type_info_impl(const FunctionType& self);

      TreePtr<Term> parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      TreePtr<Anonymous> parameter_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      
      ResultMode result_mode;
      TreePtr<Term> result_type;
      PSI_STD::vector<FunctionParameterType> parameter_types;
      PSI_STD::vector<TreePtr<InterfaceValue> > interfaces;
    };
    
    class JumpTarget;

    class Function : public ModuleGlobal {
      DelayedValue<TreePtr<Term>, TreePtr<Function> > m_body;
      void check_type();
      TreePtr<Function> get_ptr() const {return tree_from(this);}
      
    public:
      static const VtableType vtable;
      
      template<typename BodyCallback>
      Function(const TreePtr<Module>& module,
               const TreePtr<FunctionType>& type,
               bool local,
               const PSI_STD::vector<TreePtr<Anonymous> >& arguments_,
               const TreePtr<JumpTarget>& return_target_,
               const SourceLocation& location,
               const BodyCallback& body_callback)
      : ModuleGlobal(&vtable, module, type, local, location),
      m_body(module.compile_context(), location, body_callback),
      arguments(arguments_),
      return_target(return_target_) {
        check_type();
      }

      /// \brief Argument values.
      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      /**
       * \brief Target to jump to in order to return from the function.
       * 
       * This may be NULL if the function only exits ordinarily.
       */
      TreePtr<JumpTarget> return_target;
      
      /// \brief Function body.
      const TreePtr<Term>& body() const {return m_body.get(this, &Function::get_ptr);}

      template<typename Visitor> static void visit(Visitor& v);
      static void local_complete_impl(const Function& self);
    };

    /**
     * \brief Function inside another function.
     * 
     * This term exists to allow the compiler to figure out which variables are required by a closure and then
     * automatically generate a data structure containing them.
     */
    class Closure : public Term {
    public:
      static const VtableType vtable;
      
      Closure(const TreePtr<FunctionType>& type,
              const TreePtr<Anonymous>& closure_data_type,
              const PSI_STD::vector<TreePtr<Anonymous> >& arguments,
              const TreePtr<Term>& closure_data,
              const TreePtr<Term>& body,
              const TreePtr<JumpTarget>& return_target,
              const SourceLocation& location);
      
      template<typename V> static void visit(V& v);
      
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
      static const VtableType vtable;

      TryFinally(const TreePtr<Term>& try_expr, const TreePtr<Term>& finally_expr, bool except_only, const SourceLocation& location);

      template<typename Visitor> static void visit(Visitor& v);
      static TermTypeInfo type_info_impl(const TryFinally& self);

      TreePtr<Term> try_expr, finally_expr;
      /// Only run finally_expr in exceptional exits.
      bool except_only;
    };
    
    /**
     * \brief If-Then-Else.
     */
    class IfThenElse : public Functional {
    public:
      static const VtableType vtable;
      
      IfThenElse(const TreePtr<Term>& condition, const TreePtr<Term>& true_value, const TreePtr<Term>& false_value);
      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const IfThenElse& self);
      static TermTypeInfo type_info_impl(const IfThenElse& self);
      
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
      static const VtableType vtable;
      
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
      static TermResultInfo make_result_type(const TreePtr<Term>& initial, const PSI_STD::vector<TreePtr<JumpTarget> >& values, const SourceLocation& location);
    public:
      static const VtableType vtable;
      
      JumpGroup(const TreePtr<Term>& initial, const PSI_STD::vector<TreePtr<JumpTarget> >& values, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static TermTypeInfo type_info_impl(const JumpGroup& self);
      
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
      static const VtableType vtable;
      
      JumpTo(const TreePtr<JumpTarget>& target, const TreePtr<Term>& argument, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      
      TreePtr<JumpTarget> target;
      TreePtr<Term> argument;
    };

    /**
     * \brief Function invocation expression.
     */
    class FunctionCall : public Term {
      static TermResultInfo get_result_type(const TreePtr<Term>& target, PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);

    public:
      static const VtableType vtable;

      FunctionCall(const TreePtr<Term>& target, PSI_STD::vector<TreePtr<Term> >& arguments, const SourceLocation& location);
      template<typename Visitor> static void visit(Visitor& v);
      static TermTypeInfo type_info_impl(const FunctionCall& self);

      TreePtr<Term> target;
      PSI_STD::vector<TreePtr<Term> > arguments;
    };
    
    /**
     * \brief Provide a value for a phantom term during evaluation of a tree.
     */
    class SolidifyDuring : public Term {
    public:
      static const VtableType vtable;
      
      SolidifyDuring(const PSI_STD::vector<TreePtr<Term> >& value, const TreePtr<Term>& body, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const SolidifyDuring& self);

      /// \brief Value being supplied; must have type ConstantType
      PSI_STD::vector<TreePtr<Term> > value;
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
      static const VtableType vtable;
      
      PrimitiveType(const String& name);
      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const PrimitiveType& self);
      static TermTypeInfo type_info_impl(const PrimitiveType& self);

      String name;
    };
    
    /**
     * \brief Tree for built in values.
     */
    class BuiltinValue : public Constant {
    public:
      static const VtableType vtable;
      
      BuiltinValue(const String& constructor, const String& data, const TreePtr<Term>& type);
      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const BuiltinValue& self);
      static TermTypeInfo type_info_impl(const BuiltinValue& self);
      
      TreePtr<Term> builtin_type;
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
      static const VtableType vtable;
      IntegerValue(const TreePtr<Term>& type, int value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const IntegerValue& self);
      static TermTypeInfo type_info_impl(const IntegerValue& self);
      
      TreePtr<Term> integer_type;
      int value;
    };
    
    /**
     * \brief Class for string data.
     */
    class StringValue : public Constant {
    public:
      static const VtableType vtable;
      StringValue(const String& value);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const StringValue& self);
      static TermTypeInfo type_info_impl(const StringValue& self);
      
      String value;
    };
    
    /**
     * Tree for builtin operations. This saves having to create a separate tree for each one.
     */
    class BuiltinFunction : public Global {
    public:
      static const VtableType vtable;
      
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
      static const VtableType vtable;
      
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
      static const VtableType vtable;

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
      typedef PSI_STD::map<String, TreePtr<Term> > NameMapType;
      
    private:
      Namespace(CompileContext& compile_context, const NameMapType& members, const SourceLocation& location);

    public:
      static const VtableType vtable;

      Namespace(CompileContext& compile_context, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      NameMapType members;

      static TreePtr<Namespace> new_(CompileContext& compile_context, const NameMapType& members, const SourceLocation& location);
    };
    
    /**
     * \brief Get the value associated with an interface for particular parameters.
     */
    class InterfaceValue : public Functional {
    public:
      static const VtableType vtable;
      
      InterfaceValue(const TreePtr<Interface>& interface, const PSI_STD::vector<TreePtr<Term> >& parameters, const TreePtr<Implementation>& implementation);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const InterfaceValue& self);
      static TermTypeInfo type_info_impl(const InterfaceValue& self);
      static bool pure(const InterfaceValue& self);
      
      /// \brief Interface.
      TreePtr<Interface> interface;
      /// \brief List of parameters, including implicit and derived parameters.
      PSI_STD::vector<TreePtr<Term> > parameters;
      /**
       * \brief Which overload is used.
       * 
       * This is only required to be non-NULL if the interface has derived parameters, in which case
       * the overload must be identified in order to know what they are. If this is NULL the which
       * overload is to be used can be discovered at compile time; obviously this requires that the
       * list used for lookup during either stage is consistent.
       */
      TreePtr<Implementation> implementation;
    };
    
    /**
     * \brief Make the value movable.
     * 
     * If the argument to this is an lvalue ref, the result is an rvalue ref
     * of the same type.
     */
    class MovableValue : public Functional {
    public:
      static const VtableType vtable;
      
      MovableValue(const TreePtr<Term>& value);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const MovableValue& self);
      static TermTypeInfo type_info_impl(const MovableValue& self);
      
      /// \brief Argument value
      TreePtr<Term> value;
    };
    
    /**
     * \brief Initialize a value at a pointer.
     * 
     * Performs the role of a constructor in C++.
     */
    class InitializePointer : public Term {
    public:
      static const VtableType vtable;
      
      InitializePointer(const TreePtr<Term>& target_ptr, const TreePtr<Term>& assign_value, const TreePtr<Term>& inner, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const InitializePointer& self);
      
      /// \brief Pointer to address to be initialized
      TreePtr<Term> target_ptr;
      /// \brief Value to be used to initialize target
      TreePtr<Term> assign_value;
      /**
       * This tree is evaluated after constructors are run but
       * if this term throws an exception the value at the pointer
       * is destroyed.
       */
      TreePtr<Term> inner;
    };
    
    /**
     * \brief Assign a value at a pointer.
     * 
     * This assumes the memory at the pointer has already been initialized.
     */
    class AssignPointer : public Term {
    public:
      static const VtableType vtable;
      
      AssignPointer(const TreePtr<Term>& target_ptr, const TreePtr<Term>& assign_value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Pointer to address to be assigned.
      TreePtr<Term> target_ptr;
      /// \brief Value to be assign to target.
      TreePtr<Term> assign_value;
    };
    
    /**
     * \brief Finalize (destroy) the object at a pointer.
     */
    class FinalizePointer : public Term {
    public:
      static const VtableType vtable;
      
      FinalizePointer(const TreePtr<Term>& target_ptr, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Pointer to object to be destroyed.
      TreePtr<Term> target_ptr;
    };
    
    /**
     * \brief Introduce implementations which are not globally visible.
     * 
     * This is usually used to make the compiler aware of implementations passed as function parameters
     * or base implementations of such parameters.
     */
    class IntroduceImplementation : public Term {
    public:
      static const VtableType vtable;
      
      IntroduceImplementation(const PSI_STD::vector<TreePtr<Implementation> >& implementations, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const IntroduceImplementation& self);
      
      /// \brief Implementations to be introduced
      PSI_STD::vector<TreePtr<Implementation> > implementations;
      /**
       * \brief Value to be evaluated
       * 
       * InterfaceValue trees occuring in this value can use implementations introduced
       * by this tree.
       */
      TreePtr<Term> value;
    };
    
    /**
     * \brief This class allows non-pure trees to be stored as functional values without using Statement.
     *
     * This works because the lifetime of functional values may be extended until the control flow merges
     * with a path in which they are not in scope; unlike for objects generally which are constructed
     * and destroyed at fixed times.
     */
    class FunctionalEvaluate : public Term {
      static TreePtr<Term> make_result_type(const TreePtr<Term>& value, const SourceLocation& location);
    public:
      static const VtableType vtable;
      FunctionalEvaluate(const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const FunctionalEvaluate& self);
      TreePtr<Term> value;
    };
    
    /**
     * \brief Global equivalent of FunctionalEvaluate.
     * 
     * This attaches a module to a FunctionalEvaluate declaration so that when it is encountered by
     * the code generator, it is marked as being global and which module to place it in is known.
     */
    class GlobalEvaluate : public Term {
    public:
      static const VtableType vtable;
      GlobalEvaluate(const TreePtr<Module>& module, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const GlobalEvaluate& self);
      TreePtr<Module> module;
      TreePtr<Term> value;
    };
  }
}

#endif
