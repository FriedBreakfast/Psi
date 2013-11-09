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
    
    /// \brief Type passed to macros during term evaluation
    struct MacroTermArgument {typedef TreePtr<Term> EvaluateResultType;};
    
    /**
     * \brief Low-level macro interface.
     *
     * \see Macro
     * \see MacroWrapper
     */
    struct MacroVtable {
      TreeVtable base;
      void (*evaluate) (void*, const Macro*, const TreePtr<Term>*, const PSI_STD::vector<SharedPtr<Parser::Expression> >*, const TreePtr<EvaluateContext>*, const void*, const SourceLocation*);
      void (*dot) (void*, const Macro*, const TreePtr<Term>*, const SharedPtr<Parser::Expression>*, const PSI_STD::vector<SharedPtr<Parser::Expression> >*, const TreePtr<EvaluateContext>*, const void*, const SourceLocation*);
      void (*cast) (void*, const Macro*, const TreePtr<Term>*, const TreePtr<EvaluateContext>*, const void*, const SourceLocation*);
    };

    class Macro : public Tree {
    public:
      typedef MacroVtable VtableType;
      static const SIVtable vtable;

      Macro(const MacroVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      void evaluate_raw(void *result,
                        const TreePtr<Term>& value,
                        const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const void *argument,
                        const SourceLocation& location) const {
        derived_vptr(this)->evaluate(result, this, &value, &parameters, &evaluate_context, argument, &location);
      }
      
      template<typename Result, typename Arg>
      Result evaluate(const TreePtr<Term>& value,
                      const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                      const TreePtr<EvaluateContext>& evaluate_context,
                      const Arg& argument,
                      const SourceLocation& location) const {
        ResultStorage<Result> rs;
        evaluate_raw(rs.ptr(), value, parameters, evaluate_context, &argument, location);
        return rs.done();
      }
      
      void dot_raw(void *result,
                   const TreePtr<Term>& value,
                   const SharedPtr<Parser::Expression>& member,
                   const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                   const TreePtr<EvaluateContext>& evaluate_context,
                   const void *argument,
                   const SourceLocation& location) const {
        derived_vptr(this)->dot(result, this, &value, &member, &parameters, &evaluate_context, argument, &location);
      }
      
      template<typename Arg>
      typename Arg::EvaluateResultType dot(const TreePtr<Term>& value,
                                           const SharedPtr<Parser::Expression>& member,
                                           const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                           const TreePtr<EvaluateContext>& evaluate_context,
                                           const Arg& argument,
                                           const SourceLocation& location) const {
        ResultStorage<typename Arg::EvaluateResultType> rs;
        dot_raw(rs.ptr(), value, member, parameters, evaluate_context, &argument, location);
        return rs.done();
      }
      
      void cast_raw(void *result,
                    const TreePtr<Term>& value,
                    const TreePtr<EvaluateContext>& evaluate_context,
                    const void *argument,
                    const SourceLocation& location) const {
        derived_vptr(this)->cast(result, this, &value, &evaluate_context, argument, &location);
      }
      
      template<typename Arg>
      typename Arg::EvaluateResultType cast(const TreePtr<Term>& value,
                                            const TreePtr<EvaluateContext>& evaluate_context,
                                            const Arg& argument,
                                            const SourceLocation& location) const {
        ResultStorage<typename Arg::EvaluateResultType> rs;
        derived_vptr(this)->cast(rs.ptr(), this, &value, &evaluate_context, &argument, &location);
        return rs.done();
      }
      
      PSI_ATTRIBUTE((PSI_NORETURN))
      static void evaluate_impl(const void *result,
                                const Macro& self,
                                const TreePtr<Term>& value,
                                const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                const TreePtr<EvaluateContext>& evaluate_context,
                                const void *argument,
                                const SourceLocation& location);

      PSI_ATTRIBUTE((PSI_NORETURN))
      static void dot_impl(const void *result,
                           const Macro& self,
                           const TreePtr<Term>& value,
                           const SharedPtr<Parser::Expression>& member,
                           const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                           const TreePtr<EvaluateContext>& evaluate_context,
                           const void *argument,
                           const SourceLocation& location);
      
      PSI_ATTRIBUTE((PSI_NORETURN))
      static void cast_impl(const void *result,
                            const Macro& self,
                            const TreePtr<Term>& value,
                            const TreePtr<EvaluateContext>& evaluate_context,
                            const void *argument,
                            const SourceLocation& location);
      
      template<typename Arg>
      static typename Arg::EvaluateResultType evaluate_impl(const Macro& self,
                                                            const TreePtr<Term>& value,
                                                            const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                                            const TreePtr<EvaluateContext>& evaluate_context,
                                                            const Arg& argument,
                                                            const SourceLocation& location) {
        evaluate_impl(NULL, self, value, parameters, evaluate_context, &argument, location);
        PSI_FAIL("unreachable");
      }

      template<typename Arg>
      static typename Arg::EvaluateResultType dot_impl(const Macro& self,
                                                       const TreePtr<Term>& value,
                                                       const SharedPtr<Parser::Expression>& member,
                                                       const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters,
                                                       const TreePtr<EvaluateContext>& evaluate_context,
                                                       const Arg& argument,
                                                       const SourceLocation& location) {
        dot_impl(NULL, self, value, member, parameters, evaluate_context, &argument, location);
        PSI_FAIL("unreachable");
      }
      
      template<typename Arg>
      static typename Arg::EvaluateResultType cast_impl(const Macro& self,
                                                        const TreePtr<Term>& value,
                                                        const TreePtr<EvaluateContext>& evaluate_context,
                                                        const Arg& argument,
                                                        const SourceLocation& location) {
        cast_impl(NULL, self, value, evaluate_context, &argument, location);
        PSI_FAIL("unreachable");
      }
      
      static TreePtr<Term> cast_impl(const Macro& PSI_UNUSED(self),
                                     const TreePtr<Term>& value,
                                     const TreePtr<EvaluateContext>& PSI_UNUSED(evaluate_context),
                                     const MacroTermArgument& PSI_UNUSED(argument),
                                     const SourceLocation& PSI_UNUSED(location)) {
        return value;
      }

      template<typename V> static void visit(V& v) {visit_base<Tree>(v);}
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename EvalArg, typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static void evaluate(void* out, const Macro *self, const TreePtr<Term> *value, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters,
                           const TreePtr<EvaluateContext> *evaluate_context, const void *arg, const SourceLocation *location) {
        new (out) typename EvalArg::EvaluateResultType (Impl::evaluate_impl(*static_cast<const Derived*>(self), *value, *parameters, *evaluate_context, *static_cast<const EvalArg*>(arg), *location));
      }

      static void dot(void* out, const Macro *self, const TreePtr<Term> *value, const SharedPtr<Parser::Expression> *member,
                      const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context,
                      const void *arg, const SourceLocation *location) {
        new (out) typename EvalArg::EvaluateResultType (Impl::dot_impl(*static_cast<const Derived*>(self), *value, *member, *parameters, *evaluate_context, *static_cast<const EvalArg*>(arg), *location));
      }
      
      static void cast(void* out, const Macro *self, const TreePtr<Term> *value, const TreePtr<EvaluateContext> *evaluate_context,
                       const void *arg, const SourceLocation *location) {
        new (out) typename EvalArg::EvaluateResultType (Impl::cast_impl(*static_cast<const Derived*>(self), *value, *evaluate_context, *static_cast<const EvalArg*>(arg), *location));
      }
    };

#define PSI_COMPILER_MACRO(derived,name,super,eval_arg) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroWrapper<eval_arg,derived>::evaluate, \
    &MacroWrapper<eval_arg,derived>::dot, \
    &MacroWrapper<eval_arg,derived>::cast \
  }
  
    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapperRaw : NonConstructible {
      static void evaluate(void* out, const Macro *self, const TreePtr<Term> *value, const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters,
                          const TreePtr<EvaluateContext> *evaluate_context, const void *arg, const SourceLocation *location) {
        Impl::evaluate_impl(out, *static_cast<const Derived*>(self), *value, *parameters, *evaluate_context, arg, *location);
      }

      static void dot(void* out, const Macro *self, const TreePtr<Term> *value, const SharedPtr<Parser::Expression> *member,
                      const PSI_STD::vector<SharedPtr<Parser::Expression> > *parameters, const TreePtr<EvaluateContext> *evaluate_context,
                      const void *arg, const SourceLocation *location) {
        Impl::dot_impl(out, *static_cast<const Derived*>(self), *value, *member, *parameters, *evaluate_context, arg, *location);
      }
      
      static void cast(void* out, const Macro *self, const TreePtr<Term> *value, const TreePtr<EvaluateContext> *evaluate_context,
                       const void *arg, const SourceLocation *location) {
        Impl::cast_impl(out, *static_cast<const Derived*>(self), *value, *evaluate_context, arg, *location);
      }
    };
  
#define PSI_COMPILER_MACRO_RAW(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroWrapperRaw<derived>::evaluate, \
    &MacroWrapperRaw<derived>::dot, \
    &MacroWrapperRaw<derived>::cast \
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
      /// \brief Type of booleans
      TreePtr<Type> boolean_type;

      /// \brief Signed integer types
      TreePtr<Type> i8_type, i16_type, i32_type, i64_type, iptr_type;
      /// \brief Unsigned integer types
      TreePtr<Type> u8_type, u16_type, u32_type, u64_type, uptr_type;
      
      /// \brief The Macro interface.
      TreePtr<MetadataType> macro;
      /// \brief The macro interface for type values.
      TreePtr<MetadataType> type_macro;
      /// \brief The macro interface for the meta-type.
      TreePtr<MetadataType> metatype_macro;
      /// \brief Library metadata tag
      TreePtr<MetadataType> library_tag;
      /// \brief Namespace metadata tag
      TreePtr<MetadataType> namespace_tag;
      
      /// \brief Movable interface
      TreePtr<Interface> movable_interface;
      /// \brief Copyable interface
      TreePtr<Interface> copyable_interface;
      
      /// \brief Tag for evaluting a Macro to a Term
      TreePtr<Term> macro_term_tag;
      /// \brief Type for evaluating a Macro which is an aggregate member.
      TreePtr<Term> macro_member_tag;
      /// \brief Type for evaluating a Macro which is an interface member.
      TreePtr<Term> macro_interface_member_tag;
      /// \brief Type for evaluating a Macro which is an interface definition.
      TreePtr<Term> macro_interface_definition_tag;
      
      /// \brief Type used to look up macros for evaluating numbers
      TreePtr<Term> evaluate_number_tag;
      /// \brief Type used to look up macros for evaluating (...)
      TreePtr<Term> evaluate_bracket_tag;
      /// \brief Type used to look up macros for evaluating {...}
      TreePtr<Term> evaluate_brace_tag;
      /// \brief Type used to look up macros for evaluating [...]
      TreePtr<Term> evaluate_square_bracket_tag;
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
      void jit_compile_many(const PSI_STD::vector<TreePtr<Global> >& globals);

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
    class InterfaceValue;

    void compile_expression(void *result, const SharedPtr<Parser::Expression>& expression, const TreePtr<EvaluateContext>& evaluate_context, const TreePtr<Term>& mode_tag, const void *arg, const LogicalSourceLocationPtr& source);
    
    /// \copydoc compile_expression(void *result, const SharedPtr<Parser::Expression>& expression, const TreePtr<EvaluateContext>& evaluate_context, const TreePtr<Term>& arg_type, void *arg, const LogicalSourceLocationPtr& source)
    template<typename Result, typename Arg>
    Result compile_expression(const SharedPtr<Parser::Expression>& expression, const TreePtr<EvaluateContext>& evaluate_context, const TreePtr<Term>& arg_type, const Arg& arg, const LogicalSourceLocationPtr& source) {
      ResultStorage<Result> rs;
      compile_expression(rs.ptr(), expression, evaluate_context, arg_type, &arg, source);
      return rs.done();
    }
    

    TreePtr<Term> compile_block(const PSI_STD::vector<SharedPtr<Parser::Statement> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);
    TreePtr<Term> compile_from_bracket(const SharedPtr<Parser::TokenExpression>& expr, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);
    TreePtr<InterfaceValue> compile_interface_value(const SharedPtr<Parser::Expression>& , const TreePtr<EvaluateContext>& evaluate_context, const LogicalSourceLocationPtr& location);
    TreePtr<Macro> expression_macro(const TreePtr<EvaluateContext>& context, const TreePtr<Term>& expr, const TreePtr<Term>& tag_type, const SourceLocation& location);

    TreePtr<Term> compile_function_invocation(const TreePtr<Term>& function, const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);
    PSI_STD::vector<TreePtr<Term> > compile_call_arguments(const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                                           const TreePtr<EvaluateContext>& evaluate_context,
                                                           const SourceLocation& location);

    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_module(const TreePtr<Module>& module, const TreePtr<EvaluateContext>& next, const SourceLocation& location);
    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>&, const SourceLocation&, const std::map<String, TreePtr<Term> >&, const TreePtr<EvaluateContext>&);
    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>&, const SourceLocation&, const std::map<String, TreePtr<Term> >&);
    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_dictionary(const SourceLocation&, const std::map<String, TreePtr<Term> >&, const TreePtr<EvaluateContext>&);
    PSI_COMPILER_EXPORT TreePtr<EvaluateContext> evaluate_context_root(const TreePtr<Module>& module);

    PSI_COMPILER_EXPORT TreePtr<Term> compile_term(const SharedPtr<Parser::Expression>&, const TreePtr<EvaluateContext>&, const LogicalSourceLocationPtr&);
    PSI_COMPILER_EXPORT TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);

    /**
     * \brief Callback which compile_script calls on each statement.
     */
    class CompileScriptCallback {
    public:
      /**
       * \brief Called for each statement in the list.
       */
      virtual TreePtr<Term> run(unsigned index, const TreePtr<Term>& value, const SourceLocation& location) const = 0;
    };
    
    struct CompileScriptResult {
      PSI_STD::map<String, TreePtr<Term> > names;
      PSI_STD::vector<TreePtr<Global> > globals;
    };
    
    PSI_COMPILER_EXPORT CompileScriptResult compile_script(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                                                           const TreePtr<EvaluateContext>& evaluate_context, const CompileScriptCallback& callback,
                                                           const SourceLocation& location);

    template<typename T>
    class CompileScriptCallbackImpl : public CompileScriptCallback {
      const T *m_cb;
      
    public:
      CompileScriptCallbackImpl(const T& cb) : m_cb(&cb) {}

      virtual TreePtr<Term> run(unsigned index, const TreePtr<Term>& value, const SourceLocation& location) const {
        return (*m_cb)(index, value, location);
      }
    };
    
    /// \copydoc compile_script
    template<typename T>
    CompileScriptResult compile_script(const PSI_STD::vector<SharedPtr<Parser::Statement> >& statements,
                                       const TreePtr<EvaluateContext>& evaluate_context, const T& callback,
                                       const SourceLocation& location) {
      CompileScriptCallbackImpl<T> impl(callback);
      return compile_script(statements, evaluate_context, static_cast<const CompileScriptCallback&>(impl), location);
    }
  }
}

#endif
