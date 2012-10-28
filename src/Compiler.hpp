#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef PSI_DEBUG
#include <typeinfo>
#endif

#include "TreeBase.hpp"
#include "Term.hpp"
#include "Platform.hpp"

namespace Psi {
  namespace Parser {
    struct Expression;
    struct NamedExpression;
  }
  
  namespace Tvm {
    template<typename> class ValuePtr;
    class Global;
    class Jit;
  }
  
  namespace Compiler {
    class CompileException : public std::exception {
    public:
      CompileException();
      virtual ~CompileException() throw();
      virtual const char *what() const throw();
    };

    class Anonymous;
    class Global;
    class Interface;
    class Type;

    /**
     * \brief Class used for error reporting.
     */
    class CompileError {
      CompileContext *m_compile_context;
      SourceLocation m_location;
      unsigned m_flags;
      const char *m_type;

    public:
      enum ErrorFlags {
        error_warning=1,
        error_internal=2
      };

      template<typename T>
      static std::string to_str(const T& t) {
        std::ostringstream ss;
        ss << t;
        return ss.str();
      }

      CompileError(CompileContext& compile_context, const SourceLocation& location, unsigned flags=0);

      void info(const std::string& message);
      void info(const SourceLocation& location, const std::string& message);
      void end();

      const SourceLocation& location() {return m_location;}

      template<typename T> void info(const T& message) {info(to_str(message));}
      template<typename T> void info(const SourceLocation& location, const T& message) {info(location, to_str(message));}
    };


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
      const TreeBase* (*evaluate) (const Macro*, const TreeBase*, const void*, void*, const TreeBase*, const SourceLocation*);
      const TreeBase* (*dot) (const Macro*, const TreeBase*, const SharedPtr<Parser::Expression>*, const void*, void*, const TreeBase*, const SourceLocation*);
    };

    class Macro : public Tree {
    public:
      typedef MacroVtable VtableType;
      static const SIVtable vtable;

      Macro(const MacroVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value,
                             const List<SharedPtr<Parser::Expression> >& parameters,
                             const TreePtr<EvaluateContext>& evaluate_context,
                             const SourceLocation& location) const {
        return tree_from_base_take<Term>(derived_vptr(this)->evaluate(this, value.raw_get(), parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location));
      }
      
      TreePtr<Term> dot(const TreePtr<Term>& value,
                        const SharedPtr<Parser::Expression>& member,
                        const List<SharedPtr<Parser::Expression> >& parameters,
                        const TreePtr<EvaluateContext>& evaluate_context,
                        const SourceLocation& location) const {
        return tree_from_base_take<Term>(derived_vptr(this)->dot(this, value.raw_get(), &member, parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location));
      }
      
      template<typename V> static void visit(V& v) {visit_base<Tree>(v);}
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static const TreeBase* evaluate(const Macro *self,
                                      const TreeBase *value,
                                      const void *parameters_vtable,
                                      void *parameters_object,
                                      const TreeBase *evaluate_context,
                                      const SourceLocation *location) {
        TreePtr<Term> result = Impl::evaluate_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
      }

      static const TreeBase* dot(const Macro *self,
                                 const TreeBase *value,
                                 const SharedPtr<Parser::Expression> *member,
                                 const void *parameters_vtable,
                                 void *parameters_object,
                                 const TreeBase *evaluate_context,
                                 const SourceLocation *location) {
        TreePtr<Term> result = Impl::dot_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), *member, List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
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
    public:
      static const TreeVtable vtable;
      
      Module(CompileContext& compile_context, const SourceLocation& location);
      Module(CompileContext& compile_context, const String& name, const SourceLocation& location);
      template<typename V> static void visit(V& v);
      
      /// \brief Name of this module. Used for diagnositc messages only.
      String name;
    };

    /**
     * \see EvaluateContext
     */
    struct EvaluateContextVtable {
      TreeVtable base;
      void (*lookup) (LookupResult<TreePtr<Term> >*, const EvaluateContext*, const String*, const SourceLocation*, const TreeBase*);
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
        derived_vptr(this)->lookup(result.ptr(), this, &name, &location, evaluate_context.raw_get());
        return result.done();
      }

      LookupResult<TreePtr<Term> > lookup(const String& name, const SourceLocation& location) const {
        return lookup(name, location, TreePtr<EvaluateContext>(this));
      }
      
      const TreePtr<Module>& module() const {return m_module;}
      
      template<typename V> static void visit(V& v) {visit_base<Tree>(v); v("module", &EvaluateContext::m_module);}
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct EvaluateContextWrapper : NonConstructible {
      static void lookup(LookupResult<TreePtr<Term> > *result, const EvaluateContext *self, const String *name, const SourceLocation *location, const TreeBase* evaluate_context) {
        new (result) LookupResult<TreePtr<Term> >(Impl::lookup_impl(*static_cast<const Derived*>(self), *name, *location, tree_from_base<EvaluateContext>(evaluate_context)));
      }
    };

#define PSI_COMPILER_EVALUATE_CONTEXT(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &EvaluateContextWrapper<derived>::lookup \
  }

    class MacroMemberCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroMemberCallbackVtable {
      TreeVtable base;
      const TreeBase* (*evaluate) (const MacroMemberCallback*, const TreeBase*, const void*, void*, const TreeBase*, const SourceLocation*);
    };

    class MacroMemberCallback : public Tree {
    public:
      typedef MacroMemberCallbackVtable VtableType;
      static const SIVtable vtable;

      MacroMemberCallback(const MacroMemberCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& value, const List<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) const {
        return tree_from_base_take<Term>(derived_vptr(this)->evaluate(this, value.raw_get(), parameters.vptr(), parameters.object(), evaluate_context.raw_get(), &location));
      }
    };

    template<typename Derived>
    struct MacroMemberCallbackWrapper : NonConstructible {
      static const TreeBase* evaluate(const MacroMemberCallback *self, const TreeBase *value, const void *parameters_vtable, void *parameters_object, const TreeBase *evaluate_context, const SourceLocation *location) {
        TreePtr<Term> result = Derived::evaluate_impl(*static_cast<const Derived*>(self), tree_from_base<Term>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), tree_from_base<EvaluateContext>(evaluate_context), *location);
        return result.release();
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

      /// \brief The empty type.
      TreePtr<Type> empty_type;
      /// \brief The bottom type.
      TreePtr<Type> bottom_type;
      /// \brief The type of types
      TreePtr<Term> metatype;
      
      /// \brief The Macro interface.
      TreePtr<MetadataType> macro_tag;
      /// \brief Library metadata tag
      TreePtr<MetadataType> library_tag;
      /// \brief Namespace metadata tag
      TreePtr<MetadataType> namespace_tag;
    };
    
    class Function;
    class TvmCompiler;

    /**
     * \brief Context for objects used during compilation.
     *
     * This manages state which is global to the compilation and
     * compilation object lifetimes.
     */
    class CompileContext {
      friend class Object;
      friend class RunningTreeCallback;
      struct ObjectDisposer;

      std::ostream *m_error_stream;
      bool m_error_occurred;
      RunningTreeCallback *m_running_completion_stack;

      typedef boost::intrusive::list<Object, boost::intrusive::constant_time_size<false> > GCListType;
      GCListType m_gc_list;

      SourceLocation m_root_location;
      BuiltinTypes m_builtins;
      boost::shared_ptr<TvmCompiler> m_tvm_compiler;

    public:
      CompileContext(std::ostream *error_stream);
      ~CompileContext();

      /// \brief Return the stream used for error reporting.
      std::ostream& error_stream() {return *m_error_stream;}

      /// \brief Returns true if an error has occurred during compilation.
      bool error_occurred() const {return m_error_occurred;}
      /// \brief Call this to indicate an unrecoverable error occurred at some point during compilation.
      void set_error_occurred() {m_error_occurred = true;}

      void error(const SourceLocation&, const std::string&, unsigned=0);
      void error_throw(const SourceLocation&, const std::string&, unsigned=0) PSI_ATTRIBUTE((PSI_NORETURN));

      template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, CompileError::to_str(message), flags);}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, CompileError::to_str(message), flags);}

      void completion_state_push(RunningTreeCallback *state);
      void completion_state_pop();

      /// \brief Get the root location of this context.
      const SourceLocation& root_location() {return m_root_location;}
      /// \brief Get the builtin trees.
      const BuiltinTypes& builtins() {return m_builtins;}
      
      void* jit_compile(const TreePtr<Global>& global);
    };
    
    class Block;
    class Namespace;

    TreePtr<Term> compile_expression(const SharedPtr<Parser::Expression>&, const TreePtr<EvaluateContext>&, const LogicalSourceLocationPtr&);
    TreePtr<Block> compile_block(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);
    TreePtr<Namespace> compile_namespace(const PSI_STD::vector<SharedPtr<Parser::NamedExpression> >& statements, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location);

    TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>&, const SourceLocation&, const std::map<String, TreePtr<Term> >&, const TreePtr<EvaluateContext>&);
    TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>&, const SourceLocation&, const std::map<String, TreePtr<Term> >&);
    TreePtr<EvaluateContext> evaluate_context_module(const TreePtr<Module>& module, const TreePtr<EvaluateContext>& next, const SourceLocation& location);

    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroMemberCallback>&, const std::map<String, TreePtr<MacroMemberCallback> >&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const TreePtr<MacroMemberCallback>&);
    TreePtr<Macro> make_macro(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<MacroMemberCallback> >&);
    TreePtr<Term> make_macro_term(const TreePtr<Macro>& macro, const SourceLocation& location);
    
    TreePtr<Term> type_combine(const TreePtr<Term>& lhs, const TreePtr<Term>& rhs);

    TreePtr<Term> compile_function_invocation(const TreePtr<Term>& function,
                                              const List<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location);
  }
}

#endif
