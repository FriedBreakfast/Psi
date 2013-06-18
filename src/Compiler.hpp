#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#if PSI_DEBUG
#include <typeinfo>
#endif

#include "TreeBase.hpp"
#include "Term.hpp"
#include "Enums.hpp"
#include "Array.hpp"
#include "ErrorContext.hpp"
#include "PropertyValue.hpp"

namespace Psi {
  namespace Parser {
    struct Expression;
    struct Statement;
    struct TokenExpression;
  }
  
  namespace Tvm {
    template<typename> class ValuePtr;
    class Global;
    class Jit;
  }
  
  namespace Compiler {
    class Anonymous;
    class Global;
    class Interface;
    class Type;
    class Macro;
    class EvaluateContext;
    
    /**
     * \brief Low-level macro interface.
     *
     * \see Macro
     * \see MacroWrapper
     */
    struct MacroVtable {
      TreeVtable base;
      void (*evaluate) (TreePtr<Term>*, const Macro*, const TreePtr<Term>*, const PSI_STD::vector<SharedPtr<Parser::Expression> >*, const TreePtr<EvaluateContext>*, const SourceLocation*);
      void (*dot) (TreePtr<Term>*, const Macro*, const TreePtr<Term>*, const SharedPtr<Parser::Expression>*, const PSI_STD::vector<SharedPtr<Parser::Expression> >*, const TreePtr<EvaluateContext>*, const SourceLocation*);
    };

    class Macro : public Tree {
    public:
      typedef MacroVtable VtableType;
      static const SIVtable vtable;

      Macro(const MacroVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value,
                             const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                             const TreePtr<EvaluateContext>& evaluate_context,
                             const SourceLocation& location) const {
        ResultStorage<TreePtr<Term> > rs;
        derived_vptr(this)->evaluate(rs.ptr(), this, &value, &parameters, &evaluate_context, &location);
        return rs.done();
      }
      
      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const SharedPtr<Parser::Expression>& member,
                        const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) const {
        ResultStorage<TreePtr<Term> > rs;
        derived_vptr(this)->dot(rs.ptr(), this, &value, &member, &parameters, &evaluate_context, &location);
        return rs.done();
      }
      
      template<typename V> static void visit(V& v) {visit_base<Tree>(v);}
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static void evaluate(TreePtr<Term>* out, const Macro *self, const TreePtr<Term> *value, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters,
                           const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
        new (out) TreePtr<Term> (Impl::evaluate_impl(*static_cast<const Derived*>(self), *value, *parameters, *evaluate_context, *location));
      }

      static void dot(TreePtr<Term> *out, const Macro *self, const TreePtr<Term> *value, const SharedPtr<Parser::Expression> *member,
                      const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
        new (out) TreePtr<Term> (Impl::dot_impl(*static_cast<const Derived*>(self), *value, *member, *parameters, *evaluate_context, *location));
      }
    };

#define PSI_COMPILER_MACRO(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroWrapper<derived>::evaluate, \
    &MacroWrapper<derived>::dot \
  }
    
    /**
     * \brief A collection of global variables.
     */
    class Module : public Tree {
      Module(CompileContext& compile_context, const String& name, const SourceLocation& location);

    public:
      static const TreeVtable vtable;
      
      PSI_COMPILER_EXPORT static TreePtr<Module> new_(CompileContext& compile_context, const String& name, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Name of this module. Used for diagnositc messages only.
      String name;
    };
    
    class OverloadType;
    class OverloadValue;

    /**
     * \see EvaluateContext
     */
    struct EvaluateContextVtable {
      TreeVtable base;
      void (*lookup) (LookupResult<TreePtr<Term> >*, const EvaluateContext*, const String*, const SourceLocation*, const TreePtr<EvaluateContext>*);
      void (*overload_list) (const EvaluateContext*, const OverloadType*, PSI_STD::vector<TreePtr<OverloadValue> >*);
    };

    class EvaluateContext : public Tree {
      TreePtr<Module> m_module;
      
    public:
      typedef EvaluateContextVtable VtableType;
      static const SIVtable vtable;

      EvaluateContext(const EvaluateContextVtable *vptr, const TreePtr<Module>& module, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), module->compile_context(), location),
      m_module(module) {
      }

      LookupResult<TreePtr<Term> > lookup(const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) const {
        ResultStorage<LookupResult<TreePtr<Term> > > result;
        derived_vptr(this)->lookup(result.ptr(), this, &name, &location, &evaluate_context);
        return result.done();
      }

      LookupResult<TreePtr<Term> > lookup(const String& name, const SourceLocation& location) const {
        return lookup(name, location, tree_from(this));
      }
      
      /// \brief Get all overloads of a certain type.
      void overload_list(const TreePtr<OverloadType>& overload_type, PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) const {
        derived_vptr(this)->overload_list(this, overload_type.get(), &overload_list);
      }
      
      const TreePtr<Module>& module() const {return m_module;}
      
      template<typename V> static void visit(V& v) {visit_base<Tree>(v); v("module", &EvaluateContext::m_module);}
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct EvaluateContextWrapper : NonConstructible {
      static void lookup(LookupResult<TreePtr<Term> > *result, const EvaluateContext *self, const String *name, const SourceLocation *location, const TreePtr<EvaluateContext>* evaluate_context) {
        new (result) LookupResult<TreePtr<Term> >(Impl::lookup_impl(*static_cast<const Derived*>(self), *name, *location, *evaluate_context));
      }
      
      static void overload_list(const EvaluateContext *self, const OverloadType *overload_type, PSI_STD::vector<TreePtr<OverloadValue> > *overload_list) {
        Impl::overload_list_impl(*static_cast<const Derived*>(self), tree_from(overload_type), *overload_list);
      }
    };

#define PSI_COMPILER_EVALUATE_CONTEXT(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &EvaluateContextWrapper<derived>::lookup, \
    &EvaluateContextWrapper<derived>::overload_list \
  }

    class MacroMemberCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroMemberCallbackVtable {
      TreeVtable base;
      void (*evaluate) (TreePtr<Term>*, const MacroMemberCallback*, const TreePtr<Term>*, const PSI_STD::vector<SharedPtr<Parser::Expression> >*, const TreePtr<EvaluateContext>*, const SourceLocation*);
    };

    class MacroMemberCallback : public Tree {
    public:
      typedef MacroMemberCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroMemberCallback(const MacroMemberCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value, const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) const {
        ResultStorage<TreePtr<Term> > rs;
        derived_vptr(this)->evaluate(rs.ptr(), this, &value, &parameters, &evaluate_context, &location);
        return rs.done();
      }
    };

    template<typename Derived>
    struct MacroMemberCallbackWrapper : NonConstructible {
      static void evaluate(TreePtr<Term> *out, const MacroMemberCallback *self, const TreePtr<Term> *value, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context, const SourceLocation *location) {
        new (out) TreePtr<Term> (Derived::evaluate_impl(*static_cast<const Derived*>(self), *value, *parameters, *evaluate_context, *location));
      }
    };

#define PSI_COMPILER_MACRO_MEMBER_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroMemberCallbackWrapper<derived>::evaluate \
  }
    
    class MetadataType;
    
    struct BuiltinTypes {
      BuiltinTypes();
      void initialize(CompileContext& compile_context);

      /// \brief The type of types
      TreePtr<Term> metatype;
      /// \brief The empty type.
      TreePtr<Type> empty_type;
      /// \brief Value of the empty type.
      TreePtr<Term> empty_value;
      /// \brief The bottom type.
      TreePtr<Type> bottom_type;
      /// \brief The type of upward references
      TreePtr<Type> upref_type;
      /// \brief The NULL upward reference
      TreePtr<Term> upref_null;
      /// \brief Type of string elements, i.e. unsigned char
      TreePtr<Type> string_element_type;
      /// \brief Type of booleans
      TreePtr<Type> boolean_type;

      /// \brief Signed integer types
      TreePtr<Type> i8_type, i16_type, i32_type, i64_type, iptr_type;
      /// \brief Unsigned integer types
      TreePtr<Type> u8_type, u16_type, u32_type, u64_type, uptr_type;
      
      /// \brief The Macro interface.
      TreePtr<MetadataType> macro_tag;
      /// \brief Library metadata tag
      TreePtr<MetadataType> library_tag;
      /// \brief Namespace metadata tag
      TreePtr<MetadataType> namespace_tag;
      
      /// \brief Movable interface
      TreePtr<Interface> movable_interface;
      /// \brief Copyable interface
      TreePtr<Interface> copyable_interface;
    };
    
    class Function;
    class TvmJit;

    /**
     * \brief Context for objects used during compilation.
     *
     * This manages state which is global to the compilation and
     * compilation object lifetimes.
     */
    class PSI_COMPILER_EXPORT CompileContext {
      friend class Object;
      friend class Functional;
      friend class RunningTreeCallback;
      struct ObjectDisposer;

      CompileErrorContext *m_error_context;
      RunningTreeCallback *m_running_completion_stack;

      typedef boost::intrusive::list<Object, boost::intrusive::constant_time_size<false> > GCListType;
      GCListType m_gc_list;

      struct FunctionalTermHasher {std::size_t operator () (const Functional& f) const {return f.m_hash;}};
      struct FunctionalSetupEquals;
      typedef boost::intrusive::unordered_set<Functional,
                                              boost::intrusive::member_hook<Functional, boost::intrusive::unordered_set_member_hook<>, &Functional::m_set_hook>,
                                              boost::intrusive::hash<FunctionalTermHasher>,
                                              boost::intrusive::power_2_buckets<true> > FunctionalTermSetType;
      static const std::size_t initial_functional_term_buckets = 64;
      UniqueArray<FunctionalTermSetType::bucket_type> m_functional_term_buckets;
      FunctionalTermSetType m_functional_term_set;

      SourceLocation m_root_location;
      BuiltinTypes m_builtins;
      boost::shared_ptr<TvmJit> m_jit;

      TreePtr<Functional> get_functional_ptr(const Functional& f, const SourceLocation& location);
      
#if PSI_OBJECT_PTR_DEBUG
      template<typename> friend class ObjectPtr;
      static const unsigned object_ptr_backtrace_depth = 10;
      struct ObjectPtrSetValue {
        const Object *obj;
        void *backtrace[object_ptr_backtrace_depth];
      };
      typedef boost::unordered_map<void*, ObjectPtrSetValue> ObjectPtrSetType;
      ObjectPtrSetType m_object_ptr_set;
      typedef boost::unordered_map<const Object*, std::size_t> ObjectAuxiliaryCountMapType;
      ObjectAuxiliaryCountMapType m_object_aux_count_map;
      std::size_t m_object_ptr_offset;
      void object_ptr_backtrace(const ObjectPtrSetValue& value);
      void object_ptr_add(const Object *obj, void *ptr);
      void object_ptr_remove(const Object *obj, void *ptr);
      void object_ptr_move(const Object *obj, void *from, void *to);
#endif

    public:
      CompileContext(CompileErrorContext *error_context, const PropertyValue& jit_configuration);
      ~CompileContext();
      
#if PSI_DEBUG
      std::set<void*> object_pointers();
#endif

      /// \brief Get the root location of this context.
      const SourceLocation& root_location() {return m_root_location;}
      /// \brief Get the builtin trees.
      const BuiltinTypes& builtins() {return m_builtins;}
      
      void* jit_compile(const TreePtr<Global>& global);

      template<typename T>
      TreePtr<T> get_functional(const T& t, const SourceLocation& location) {
        return treeptr_cast<T>(get_functional_ptr(t, location));
      }
      
      /// \brief Get the error reporting context
      CompileErrorContext& error_context() {return *m_error_context;}
      
      /// Forwards to CompileErrorContext::error_throw
      PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const std::string& message, unsigned flags=0) {error_context().error_throw(loc, message, flags);}
      /// Forwards to CompileErrorContext::error_throw
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_context().error_throw(loc, message, flags);}
    };
    
    class Block;
    class Namespace;

    PSI_COMPILER_EXPORT TreePtr<Term> compile_expression(const SharedPtr<Parser::Expression>&, const TreePtr<EvaluateContext>&, const LogicalSourceLocationPtr&);
    PSI_COMPILER_EXPORT TreePtr<Term> compile_block(const PSI_STD::vector<SharedPtr<Parser::Statement> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);
    PSI_COMPILER_EXPORT TreePtr<Term> compile_from_bracket(const SharedPtr<Parser::TokenExpression>& expr, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);

    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>&, const SourceLocation&, const std::map<String, TreePtr<Term> >&, const TreePtr<EvaluateContext>&);
    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>&, const SourceLocation&, const std::map<String, TreePtr<Term> >&);
    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_module(const TreePtr<Module>& module, const TreePtr<EvaluateContext>& next, const SourceLocation& location);

    PSI_COMPILER_EXPORT TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroMemberCallback>&, const std::map<String, TreePtr<MacroMemberCallback> >&);
    PSI_COMPILER_EXPORT TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroMemberCallback>&);
    PSI_COMPILER_EXPORT TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<MacroMemberCallback> >&);
    PSI_COMPILER_EXPORT TreePtr<Term> make_macro_term(const TreePtr<Macro>& macro, const SourceLocation& location);
    
    PSI_COMPILER_EXPORT TreePtr<Term> type_combine(const TreePtr<Term>& lhs, const TreePtr<Term>& rhs);

    PSI_COMPILER_EXPORT TreePtr<Term> compile_function_invocation(const TreePtr<Term>& function, const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                                                  const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);
  }
}

#endif
