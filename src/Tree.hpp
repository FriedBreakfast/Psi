#ifndef HPP_PSI_TREE
#define HPP_PSI_TREE

#include <vector>

#include "Compiler.hpp"
#include "StaticDispatch.hpp"
#include "Enums.hpp"
#include "PropertyValue.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * \brief Base for explicit constructor expressions.
     */
    class Constructor : public Functional {
    public:
      PSI_COMPILER_EXPORT static const SIVtable vtable;
      
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
      PSI_COMPILER_EXPORT static const SIVtable vtable;
      
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

    PSI_SMALL_ENUM(Linkage) {
      /**
        * No linkage.
        * 
        * This is used for things like aliases which do not produce a symbol
        * of any kind.
        */
      link_none,
      /**
        * Local data: not visible outside the object file in which it is defined.
        * 
        * This data is local to a function or some other definition, so it
        * is never merged with symbols from another object (except in that
        * constant merging may apply; see GlobalVariable::merge).
        */
      link_local,
      /**
        * Not visible outside the shared library in which it is defined.
        */
      link_private,
      /**
        * Weak linkage; used for specialization instantiations.
        * 
        * Multiple definitions is not an error; however the one definition rule applies:
        * all definitions must be equivalent.
        */
      link_one_definition,
      /**
       * \brief Public symbol; exported from a shared library
       * 
       * The compiler will automatically work out whether import or export linkage
       * is required (i.e. dllimport/dllexport on Windows since Linux doesn't care)
       * from the modules which each symbol appears in.
       */
      link_public
    };
    PSI_VISIT_SIMPLE(Linkage)
    
    /**
     * \brief A global variable or function, which is an element of a Module.
     */
    class ModuleGlobal : public Global {
    public:
      PSI_COMPILER_EXPORT static const SIVtable vtable;
      PSI_COMPILER_EXPORT ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module, const String& symbol_name,
                                       const TreePtr<Term>& type, Linkage linkage, const SourceLocation& location);
      ModuleGlobal(const VtableType *vptr, const TreePtr<Module>& module, const TermResultInfo& type, Linkage linkage, const SourceLocation& location);
      
      template<typename V> static void visit(V& v);
      
      /// \brief Get the module this global should be built into.
      TreePtr<Module> module;
      /// \brief If set, this variable does not have a symbol name.
      Linkage linkage;
      /// \brief If non-empty, use this instead of the normally generated symbol name.
      String symbol_name;
    };
    
    /**
     * \brief Used to represent globals in modules not being compiled.
     * 
     * This removes the requirement for specifying a value or function body; only a type need be specified.
     */
    class ExternalGlobal : public ModuleGlobal {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      ExternalGlobal(const TreePtr<Module>& module, const String& symbol_name, const TreePtr<Term>& type, const SourceLocation& location);
      template<typename V> static void visit(V& v);
    };
    
    /**
     * \brief Equivalent to a statement, but as part of a Module rather than a function.
     */
    class GlobalStatement : public ModuleGlobal {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      GlobalVariable(const TreePtr<Module>& module, const String& symbol_name,
                     const TreePtr<Term>& type, Linkage linkage, bool constant_, bool merge_,
                     const SourceLocation& location, const ValueCallback& value)
      : ModuleGlobal(&vtable, module, symbol_name, type, linkage, location),
      m_value(module->compile_context(), location, value),
      constant(constant_),
      merge(merge_) {
      }

      PSI_COMPILER_EXPORT ~GlobalVariable();
      
      /// \brief Whether the contents of this variable are constant
      PsiBool constant;
      /// \brief Whether address of this variable may alias another variable.
      PsiBool merge;
      
      /// \brief Global variable value.
      const TreePtr<Term>& value() const {
        return m_value.get(*this, &GlobalVariable::get_ptr);
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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      DelayedValue<int, TreePtr<GenericType> > m_primitive_mode;
      TreePtr<GenericType> get_ptr() const {return tree_from(this);}

    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

      enum GenericTypePrimitive {
        primitive_recurse=0, ///< Primitive if the evaluated member_type is primitive
        primitive_never=1, ///< Never primitive
        primitive_always=2, ///< Always primitive
      };

      template<typename T, typename U, typename V>
      GenericType(CompileContext& compile_context, const PSI_STD::vector<TreePtr<Term> >& pattern_,
                  const T& primitive_mode, const U& member_callback, const V& overloads_callback, const SourceLocation& location)
      : Tree(&vtable, compile_context, location),
      m_member(compile_context, location, member_callback),
      m_overloads(compile_context, location, overloads_callback),
      m_primitive_mode(compile_context, location, primitive_mode),
      pattern(pattern_) {
      }

      template<typename Visitor> static void visit(Visitor& v);
      static void local_complete_impl(const GenericType& self);

      /// \brief Parameters pattern.
      PSI_STD::vector<TreePtr<Term> > pattern;

      /// \brief Primitive mode: whether or not this type is primitive
      GenericTypePrimitive primitive_mode() const {
        return static_cast<GenericTypePrimitive>(m_primitive_mode.get(*this, &GenericType::get_ptr));
      }
      
      /// \brief Single member of this type.
      const TreePtr<Term>& member_type() const {
        return m_member.get(*this, &GenericType::get_ptr);
      }
      
      /// \brief If the member type is currently being built.
      bool member_running() const {
        return m_member.running();
      }
      
      /// \brief Overloads carried by this type.
      const PSI_STD::vector<TreePtr<OverloadValue> >& overloads() const {
        return m_overloads.get(*this, &GenericType::get_ptr);
      }
    };
    
    PSI_VISIT_SIMPLE(GenericType::GenericTypePrimitive);

    /**
     * \brief Instance of GenericType.
     */
    class TypeInstance : public Type {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      PointerType(const TreePtr<Term>& target_type, const TreePtr<Term>& upref, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const PointerType& self);
      static TermTypeInfo type_info_impl(const PointerType& self);
      
      /// \brief Get the type referenced by this pointer type.
      TreePtr<Term> target_type;
      /// \brief Upward reference information.
      TreePtr<Term> upref;
    };
    
    /**
     * \brief Convert a reference to a pointer
     */
    class PointerTo : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
     * \brief Get a reference to a member from a reference to an aggregate value.
     */
    class ElementValue : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
     * \brief Get a pointer to a member from a pointer to an aggregate value.
     */
    class ElementPointer : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      ElementPointer(const TreePtr<Term>& value, const TreePtr<Term>& index);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ElementPointer& self);
      static TermTypeInfo type_info_impl(const ElementPointer& self);
      
      /// \brief Pointer to aggregate.
      TreePtr<Term> pointer;
      /// \brief Index of member to get.
      TreePtr<Term> index;
    };
    
    /**
     * \brief Get a reference to a containing structure from a reference to an inner value.
     */
    class OuterPointer : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      OuterPointer(const TreePtr<Term>& pointer);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const OuterPointer& self);
      static TermTypeInfo type_info_impl(const OuterPointer& self);
      
      /// \brief Pointer to data structure, which must have type Pointer
      TreePtr<Term> pointer;
    };

    /**
     * \brief Structure type.
     *
     * Stores a list of other types.
     */
    class StructType : public Type {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
     * \brief Value of NULL upward references.
     */
    class UpwardReferenceNull : public Constant {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      UpwardReferenceNull();
      static TermResultInfo check_type_impl(const UpwardReferenceNull& self);
      static TermTypeInfo type_info_impl(const UpwardReferenceNull& self);
      template<typename V> static void visit(V& v);
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
      PSI_COMPILER_EXPORT static const VtableType vtable;

      Exists(const TreePtr<Term>& result_type, const PSI_STD::vector<TreePtr<Term> >& parameter_types, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const Exists& self);
      static TermTypeInfo type_info_impl(const Exists& self);

      TreePtr<Term> parameter_type_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      TreePtr<Term> result_after(const SourceLocation& location, const PSI_STD::vector<TreePtr<Term> >& arguments) const;
      
      /// \brief Type of the inner value.
      TreePtr<Term> result;
      /// \brief Parameter types
      PSI_STD::vector<TreePtr<Term> > parameter_types;
    };
    
    /**
     * \brief Term which represents the value of an Exists parameter.
     */
    class ExistsParameter : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      ExistsParameter(const TreePtr<Term>& exists, unsigned index, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ExistsParameter& self);
      static TermTypeInfo type_info_impl(const ExistsParameter& self);
      
      /// \brief A term whose type is an Exists term.
      TreePtr<Term> exists;
      /// \brief Parameter index
      unsigned index;
    };
    
    /**
     * \brief Term which represents the inner value of an Exists term.
     */
    class ExistsValue : public Functional {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

      ExistsValue(const TreePtr<Term>& exists, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermResultInfo check_type_impl(const ExistsValue& self);
      static TermTypeInfo type_info_impl(const ExistsValue& self);
      
      /// \brief Exists term that this is the constructed value of
      TreePtr<Term> exists;
    };
    
    class FunctionType : public ParameterizedType {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT TreePtr<Function> get_ptr() const;
      PSI_COMPILER_EXPORT void check_type();
      PSI_COMPILER_EXPORT void body_check_type(TreePtr<Term>& body) const;
      
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      template<typename BodyCallback>
      Function(const TreePtr<Module>& module,
               const String& symbol_name,
               const TreePtr<FunctionType>& type,
               Linkage linkage,
               const PSI_STD::vector<TreePtr<Anonymous> >& arguments_,
               const TreePtr<JumpTarget>& return_target_,
               const SourceLocation& location,
               const BodyCallback& body_callback)
      : ModuleGlobal(&vtable, module, symbol_name, type, linkage, location),
      m_body(module->compile_context(), location, body_callback),
      arguments(arguments_),
      return_target(return_target_) {
        check_type();
      }

      PSI_COMPILER_EXPORT ~Function();

      /// \brief Argument values.
      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      /**
       * \brief Target to jump to in order to return from the function.
       * 
       * This may be NULL if the function only exits ordinarily.
       */
      TreePtr<JumpTarget> return_target;
      
      /// \brief Function body.
      const TreePtr<Term>& body() const {return m_body.get(*this, &Function::get_ptr, &Function::body_check_type);}

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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
    class NumberType : public Type {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      enum ScalarType {
        n_bool, ///< Boolean
        n_i8, ///< 8-bit signed integer
        n_i16, ///< 16-bit signed integer
        n_i32, ///< 32-bit signed integer
        n_i64, ///< 64-bit signed integer
        n_iptr, ///< Pointer sized signed integer
        n_u8, ///< 8-bit unsigned integer
        n_u16, ///< 16-bit unsigned integer
        n_u32, ///< 32-bit unsigned integer
        n_u64, ///< 64-bit unsigned integer
        n_uptr, ///< Pointer sized unsigned integer
        n_f32, ///< 32-bit float
        n_f64 ///< 64-bit float
      };

      static bool is_number(unsigned key);
      static bool is_integer(unsigned key);
      static bool is_signed(unsigned key);
      
      NumberType(ScalarType scalar_type, unsigned vector_size=0);
      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const NumberType& self);
      static TermTypeInfo type_info_impl(const NumberType& self);

      /// \brief Scalar type.
      unsigned scalar_type;
      /**
       * \brief Number of elements of this type if it is a vector.
       * 
       * Zero implies a scalar.
       */
      unsigned vector_size;
    };
    
    /**
     * \brief Tree for constant integers.
     */
    class IntegerConstant : public Constant {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      IntegerConstant(NumberType::ScalarType type, uint64_t bits);
      template<typename Visitor> static void visit(Visitor& v);
      static TermResultInfo check_type_impl(const IntegerConstant& self);
      static TermTypeInfo type_info_impl(const IntegerConstant& self);
      
      /// \brief Number type (see NumberType::ScalarType)
      unsigned number_type;
      /**
       * \brief Value.
       * 
       * Bits not beyong those expected for the type are not defined, except for boolean, where zero is false and any other value is true.
       */
      uint64_t value;
    };
    
    /**
     * \brief Class for string data.
     */
    class StringValue : public Constant {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const SIVtable vtable;
      
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
    
#define PSI_COMPILER_TARGET_CALLBACK(derived,name,super) { \
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;

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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
       * the overload must be identified in order to know what they are. If this is NULL which
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
    class InitializeValue : public Term {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      InitializeValue(const TreePtr<Term>& target_ref, const TreePtr<Term>& assign_value, const TreePtr<Term>& inner, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const InitializeValue& self);
      
      /// \brief Reference to address to be initialized
      TreePtr<Term> target_ref;
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
    class AssignValue : public Term {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      AssignValue(const TreePtr<Term>& target_ref, const TreePtr<Term>& assign_value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Pointer to address to be assigned.
      TreePtr<Term> target_ref;
      /// \brief Value to be assign to target.
      TreePtr<Term> assign_value;
    };
    
    /**
     * \brief Finalize (destroy) the object at a pointer.
     */
    class FinalizeValue : public Term {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
      FinalizeValue(const TreePtr<Term>& target_ref, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Pointer to object to be destroyed.
      TreePtr<Term> target_ref;
    };
    
    /**
     * \brief Introduce implementations which are not globally visible.
     * 
     * This is usually used to make the compiler aware of implementations passed as function parameters
     * or base implementations of such parameters.
     */
    class IntroduceImplementation : public Term {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      
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
      PSI_COMPILER_EXPORT static const VtableType vtable;
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
    class GlobalEvaluate : public ModuleGlobal {
    public:
      PSI_COMPILER_EXPORT static const VtableType vtable;
      GlobalEvaluate(const TreePtr<Module>& module, const TreePtr<Term>& value, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      static TermTypeInfo type_info_impl(const GlobalEvaluate& self);
      TreePtr<Term> value;
    };
  }
}

#endif
