#ifndef HPP_PSI_COMPILER
#define HPP_PSI_COMPILER

#include <list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <boost/function.hpp>
#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "CppCompiler.hpp"
#include "GarbageCollection.hpp"
#include "Runtime.hpp"
#include "Utility.hpp"

namespace Psi {
  namespace Parser {
    struct Expression;
    struct NamedExpression;
  }
  
  namespace Compiler {
    class CompileException : public std::exception {
    public:
      CompileException();
      virtual ~CompileException() throw();
      virtual const char *what() const throw();
    };

    struct SourceFile {
      String url;
    };

    struct PhysicalSourceLocation {
      SharedPtr<SourceFile> file;
      int first_line;
      int first_column;
      int last_line;
      int last_column;
    };

    struct LogicalSourceLocation {
      SharedPtr<LogicalSourceLocation> parent;
      String name;
    };

    struct SourceLocation {
      PhysicalSourceLocation physical;
      SharedPtr<LogicalSourceLocation> logical;

      SourceLocation(const PhysicalSourceLocation& physical_,  const SharedPtr<LogicalSourceLocation>& logical_)
      : physical(physical_), logical(logical_) {}
    };

    class Tree;
    class Type;
    class CompileContext;

    template<typename T=Tree>
    class TreePtr : public PointerBase<T> {
    public:
      TreePtr() {}
      explicit TreePtr(T *ptr) {this->reset(ptr, true);}
      TreePtr(T *ptr, bool add_ref) {this->reset(ptr, add_ref);}
      TreePtr(const TreePtr& src) : PointerBase<T>() {this->reset(src.get());}
      template<typename U> TreePtr(const TreePtr<U>& src) : PointerBase<T>() {this->reset(src.get());}
      TreePtr& operator = (const TreePtr& src) {this->reset(src.get()); return *this;}
      template<typename U> TreePtr& operator = (const TreePtr<U>& src) {this->reset(src.get()); return *this;}

      void swap(TreePtr<T>& other) {
        std::swap(this->m_ptr, other.m_ptr);
      }

      T* release() {
        T *ptr = this->m_ptr;
        this->m_ptr = 0;
        return ptr;
      }

      void reset(T* =0, bool=true);
    };

    /**
     * \brief Single inheritance dispatch table base.
     */
    struct SIVtable {
      const SIVtable *super;
      const char *classname;
      bool abstract;
    };

#define PSI_COMPILER_SI(classname,super) {reinterpret_cast<const SIVtable*>(super),classname,false}
#define PSI_COMPILER_ABSTRACT(classname,super) {reinterpret_cast<const SIVtable*>(super),classname,true}

    /**
     * \brief Single inheritance base.
     */
    class SIBase {
      friend bool si_is_a(SIBase*, const SIVtable*);
      
    protected:
      const SIVtable *m_vptr;
    };

    bool si_is_a(SIBase*, const SIVtable*);

    class VisitorPlaceholder {
    public:
      template<typename T>
      VisitorPlaceholder& operator () (const char */*name*/, T& /*member*/) {
        return *this;
      }
    };

    struct Dependency;

    struct DependencyVtable {
      void (*run) (Dependency*, Tree*);
      void (*gc_visit) (Dependency*);
      void (*destroy) (Dependency*);
    };

    class Dependency {
    protected:
      const DependencyVtable *m_vptr;

    public:
      void run(Tree *tree) {
        m_vptr->run(this, tree);
      }

      void destroy() {
        m_vptr->destroy(this);
      }
    };

    class DependencyPtr : public PointerBase<Dependency> {
      Dependency *m_ptr;

    public:
      DependencyPtr() {}
      DependencyPtr(Dependency *ptr) : PointerBase<Dependency>(ptr) {}
      ~DependencyPtr() {clear();}
      void clear() {if (m_ptr) {m_ptr->destroy(); m_ptr = 0;}}
      void swap(DependencyPtr& src) {std::swap(m_ptr, src.m_ptr);}
    };

    class CompletionState : boost::noncopyable {
      enum State {
        completion_constructed,
        completion_running,
        completion_finished,
        completion_failed
      };

      char m_state;
      
      template<typename A, typename B>
      void complete_main(const A& body, const B& cleanup) {
        try {
          m_state = completion_running;
          body();
          m_state = completion_finished;
        } catch (...) {
          m_state = completion_failed;
          cleanup();
          throw CompileException();
        }
        cleanup();
      }

      struct EmptyCallback {void operator () () const {}};

    public:
      CompletionState() : m_state(completion_constructed) {}

      template<typename A, typename B>
      void complete(CompileContext&, const SourceLocation&, bool, const A&, const B&);

      template<typename A>
      void complete(CompileContext& compile_context, const SourceLocation& location, bool dependency, const A& body) {
        complete(compile_context, location, dependency, body, EmptyCallback());
      }
    };

    struct TreeVtable {
      SIVtable base;
      void (*destroy) (Tree*);
      void (*gc_increment) (Tree*);
      void (*gc_decrement) (Tree*);
      void (*gc_clear) (Tree*);
      void (*complete_callback) (Tree*);
    };

    class Tree : public SIBase {
      template<typename> friend class TreePtr;

      std::size_t m_reference_count;
      CompileContext *m_compile_context;
      SourceLocation m_location;
      CompletionState m_completion_state;

      const TreeVtable* derived_vptr() {return reinterpret_cast<const TreeVtable*>(m_vptr);}
    public:
      static const TreeVtable vtable;

      Tree(CompileContext&, const SourceLocation&);
      
      void destroy() {derived_vptr()->destroy(this);}
      void gc_increment() {derived_vptr()->gc_increment(this);}
      void gc_decrement() {derived_vptr()->gc_decrement(this);}
      void gc_clear() {derived_vptr()->gc_clear(this);}

      /// \brief Return the compilation context this tree belongs to.
      CompileContext& compile_context() const {return *m_compile_context;}
      /// \brief Get the location associated with this tree
      const SourceLocation& location() const {return m_location;}

      void complete(bool=false);

      template<typename Visitor> static void visit_impl(Tree&, Visitor&);
      static void complete_callback_impl(Tree&);
    };

    template<typename T>
    T* tree_cast(Tree *ptr) {
      PSI_ASSERT(si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable)));
      return static_cast<T*>(ptr);
    }

    template<typename T>
    T* dyn_tree_cast(Tree *ptr) {
      return si_is_a(ptr, reinterpret_cast<const SIVtable*>(&T::vtable)) ? static_cast<T*>(ptr) : 0;
    }

    template<typename T, typename U>
    TreePtr<T> treeptr_cast(const TreePtr<U>& ptr) {
      return TreePtr<T>(tree_cast<T>(ptr.get()));
    }

    template<typename T, typename U>
    TreePtr<T> dyn_treeptr_cast(const TreePtr<U>& ptr) {
      return TreePtr<T>(dyn_tree_cast<T>(ptr.get()));
    }

    template<typename Derived>
    struct TreeWrapper {
      static void destroy(Tree *self) {
        delete static_cast<Derived*>(self);
      }

      static void gc_increment(Tree *self) {
        VisitorPlaceholder p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_decrement(Tree *self) {
        VisitorPlaceholder p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void gc_clear(Tree *self) {
        VisitorPlaceholder p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void complete_callback(Tree *self) {
        Derived::complete_callback_impl(*static_cast<Derived*>(self));
      }
    };

#define PSI_COMPILER_TREE(derived,name,super) { \
    PSI_COMPILER_SI(name,&super::vtable), \
    &TreeWrapper<derived>::destroy, \
    &TreeWrapper<derived>::gc_increment, \
    &TreeWrapper<derived>::gc_decrement, \
    &TreeWrapper<derived>::gc_clear, \
    &TreeWrapper<derived>::complete_callback \
  }

    template<typename T>
    void TreePtr<T>::reset(T *ptr, bool add_ref) {
      if (this->m_ptr) {
        if (--this->m_ptr->m_reference_count)
          this->m_ptr->destroy();
        this->m_ptr = 0;
      }

      if (ptr) {
        this->m_ptr = ptr;
        if (add_ref)
          ++this->m_ptr->m_reference_count;
      }
    }

    /**
     * \brief Base class to simplify implementing Dependency in C++.
     */
    template<typename Derived, typename TreeType>
    struct DependencyWrapper : NonConstructible {
      static void run(Dependency *self, Tree *target) {
        Derived::run_impl(*static_cast<Derived*>(self), TreePtr<TreeType>(tree_cast<TreeType>(target)));
      }

      static void gc_visit(Dependency *self) {
        VisitorPlaceholder p;
        Derived::visit_impl(*static_cast<Derived*>(self), p);
      }

      static void destroy(Dependency *self) {
        delete static_cast<Derived*>(self);
      }
    };

#define PSI_DEPENDENCY(derived,tree) { \
    &DependencyWrapper<derived, tree>::run, \
    &DependencyWrapper<derived, tree>::gc_visit, \
    &DependencyWrapper<derived, tree>::destroy \
  }

    class Expression;

    struct ExpressionVtable {
      TreeVtable base;
      void (*match) (Expression*,Expression*);
      void (*rewrite) (Expression*);
    };

    /**
     * \brief A tree which can participate in pattern matching.
     *
     * This can then be used to find interfaces.
     */
    class Expression : public Tree {
      TreePtr<Expression> m_meta;
      
    public:
      static const SIVtable vtable;
      
      Expression(CompileContext&, const SourceLocation&);

      const TreePtr<Expression>& meta() const {return m_meta;}
    };

    template<typename Derived>
    struct ExpressionWrapper : NonConstructible {
      static void match(Expression *left, Expression *right) {
        Derived::match_impl(*treeptr_cast<Derived*>(left), *treeptr_cast<Derived*>(right));
      }

      static void rewrite(Expression *self) {
        Derived::rewrite_impl(*treeptr_cast<Derived*>(self));
      }
    };

#define PSI_COMPILER_EXPRESSION(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &ExpressionWrapper<derived>::match, \
    &ExpressionWrapper<derived>::rewrite \
  }

    class Type;

    struct ValueVtable {
      ExpressionVtable base;
    };

    class Value : public Expression {
      TreePtr<Type> m_type;

    public:
      static const SIVtable vtable;

      Value(const TreePtr<Type>&, const SourceLocation&);

      /// \brief Get the type of this tree
      const TreePtr<Type>& type() const {return m_type;}
      
      template<typename Visitor> static void visit_impl(Value&, Visitor&);
    };

#define PSI_COMPILER_VALUE(derived,name,super) { \
    PSI_COMPILER_EXPRESSION(derived,name,super) \
  }

    struct TypeVtable {
      ExpressionVtable base;
      Type* (*rewrite) (Type*, const SourceLocation*, const Map<TreePtr<Type>, TreePtr<Type> >* substitutions);
    };

    class Type : public Expression {
      const TypeVtable* derived_vptr() {return reinterpret_cast<const TypeVtable*>(m_vptr);}
    public:
      static const SIVtable vtable;

      Type(CompileContext&, const SourceLocation&);

      TreePtr<Type> rewrite(const SourceLocation&, const Map<TreePtr<Type>, TreePtr<Type> >&);
    };

#define PSI_COMPILER_TYPE(derived,name,super) { \
    PSI_COMPILER_EXPRESSION(derived,name,super), \
    &TypeWrapper<derived>::rewrite \
  }

    class GlobalTree;
    class Interface;

    class CompileContext {
      friend class Tree;

      std::ostream *m_error_stream;
      bool m_error_occurred;

      template<typename T>
      static std::string to_str(const T& t) {
        std::stringstream ss;
        ss << t;
        return ss.str();
      }

    public:
      CompileContext(std::ostream *error_stream);
      ~CompileContext();

      enum ErrorFlags {
        error_warning=1,
        error_internal=2
      };

      /// \brief Returns true if an error has occurred during compilation.
      bool error_occurred() const {return m_error_occurred;}
      
      void error(const SourceLocation&, const std::string&, unsigned=0);
      void error_throw(const SourceLocation&, const std::string&, unsigned=0) PSI_ATTRIBUTE((PSI_NORETURN));

      template<typename T> void error(const SourceLocation& loc, const T& message, unsigned flags=0) {error(loc, to_str(message), flags);}
      template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error_throw(const SourceLocation& loc, const T& message, unsigned flags=0) {error_throw(loc, to_str(message), flags);}

      void* jit_compile(const TreePtr<GlobalTree>&);

      TreePtr<GlobalTree> tree_from_address(const SourceLocation&, const TreePtr<Type>&, void*);
      
      const TreePtr<Interface>& macro_interface();
      const TreePtr<Interface>& argument_passing_info_interface();
      const TreePtr<Type>& empty_type();
    };

    template<typename A, typename B>
    void CompletionState::complete(CompileContext& compile_context, const SourceLocation& location, bool dependency, const A& body, const B& cleanup) {
      switch (m_state) {
      case completion_constructed: complete_main(body, cleanup); break;
      case completion_running:
        if (!dependency)
          compile_context.error_throw(location, "Circular dependency during code evaluation");
        break;

      case completion_finished: break;
      case completion_failed: throw CompileException();
      default: PSI_FAIL("unknown future state");
      }
    }

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
      Expression* (*evaluate) (Macro*, Expression*, const void*, void*, EvaluateContext*, const SourceLocation*);
      Expression* (*dot) (Macro*, Expression*, const SharedPtr<Parser::Expression>*,  EvaluateContext*, const SourceLocation*);
    };

    class Macro : public Tree {
      const MacroVtable *derived_vptr() {return reinterpret_cast<const MacroVtable*>(m_vptr);}
    public:
      static const MacroVtable vtable;

      Macro(CompileContext&, const SourceLocation&);

      TreePtr<Expression> evaluate(const TreePtr<Expression>& value,
                                   const List<SharedPtr<Parser::Expression> >& parameters,
                                   const TreePtr<EvaluateContext>& evaluate_context,
                                   const SourceLocation& location) {
        return TreePtr<Expression>(derived_vptr()->evaluate(this, value.get(), parameters.vptr(), parameters.object(), evaluate_context.get(), &location), false);
      }

      TreePtr<Expression> dot(const TreePtr<Expression>& value,
                              const SharedPtr<Parser::Expression>& parameter,
                              const TreePtr<EvaluateContext>& evaluate_context,
                              const SourceLocation& location) {
        return TreePtr<Expression>(derived_vptr()->dot(this, value.get(), &parameter, evaluate_context.get(), &location), false);
      }
    };

    /**
     * \brief Wrapper to simplify implementing MacroVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroWrapper : NonConstructible {
      static Expression* evaluate(Macro *self,
                                  Expression *value,
                                  const void *parameters_vtable,
                                  void *parameters_object,
                                  EvaluateContext *evaluate_context,
                                  const SourceLocation *location) {
        return Impl::evaluate_impl(*static_cast<Derived*>(self), TreePtr<Expression>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), TreePtr<EvaluateContext>(evaluate_context), *location).release();
      }

      static Expression* dot(Macro *self,
                             Expression *value,
                             const SharedPtr<Parser::Expression> *parameter,
                             EvaluateContext *evaluate_context,
                             const SourceLocation *location) {
        return Impl::dot_impl(*static_cast<Derived*>(self), TreePtr<Expression>(value), *parameter, TreePtr<EvaluateContext>(evaluate_context), *location).release();
      }
    };

#define PSI_COMPILER_MACRO(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &MacroWrapper<derived>::evaluate, \
    &MacroWrapper<derived>::dot \
  }

    /**
     * \see EvaluateContext
     */
    struct EvaluateContextVtable {
      TreeVtable base;
      void (*lookup) (LookupResult<TreePtr<Expression> >*, EvaluateContext*, const String*);
    };

    class EvaluateContext : public Tree {
      const EvaluateContextVtable* derived_vptr() {return reinterpret_cast<const EvaluateContextVtable*>(m_vptr);}
    public:
      static const EvaluateContextVtable vtable;

      EvaluateContext(CompileContext&, const SourceLocation&);
      EvaluateContext();

      LookupResult<TreePtr<Expression> > lookup(const String& name) {
        ResultStorage<LookupResult<TreePtr<Expression> > > result;
        derived_vptr()->lookup(result.ptr(), this, &name);
        return result.done();
      }
    };

    /**
     * \brief Wrapper to simplify implementing EvaluateContextVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct EvaluateContextWrapper : NonConstructible {
      static void lookup(LookupResult<TreePtr<Expression> > *result, EvaluateContext *self, const String *name) {
        new (result) LookupResult<TreePtr<Expression> >(Impl::lookup_impl(*static_cast<Derived*>(self), *name));
      }
    };

#define PSI_COMPILER_EVALUATE_CONTEXT(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &EvaluateContextWrapper<derived>::lookup \
  }

    class MacroEvaluateCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroEvaluateCallbackVtable {
      TreeVtable base;
      Expression* (*evaluate) (MacroEvaluateCallback*, Expression*, const void*, void*, EvaluateContext*, const SourceLocation*);
    };

    class MacroEvaluateCallback : public Tree {
      const MacroEvaluateCallbackVtable* derived_vptr() {return reinterpret_cast<const MacroEvaluateCallbackVtable*>(m_vptr);}
    public:
      static const MacroEvaluateCallbackVtable vtable;

      TreePtr<Expression> evaluate(const TreePtr<Expression>& value, const List<SharedPtr<Parser::Expression> >& parameters, const TreePtr<EvaluateContext>& evaluate_context, const SourceLocation& location) {
        return TreePtr<Expression>(derived_vptr()->evaluate(this, value.get(), parameters.vptr(), parameters.object(), evaluate_context.get(), &location), false);
      }
    };

    template<typename Derived, typename Impl=Derived>
    struct MacroEvaluateCallbackWrapper : NonConstructible {
      static Expression* evaluate(MacroEvaluateCallback *self, Expression *value, ListVtable *parameters_vtable, ListObject *parameters_object, EvaluateContext *evaluate_context, const SourceLocation *location) {
        return Impl::evaluate(*static_cast<Derived*>(self), TreePtr<Expression>(value), List<SharedPtr<Parser::Expression> >(parameters_vtable, parameters_object), TreePtr<EvaluateContext>(evaluate_context), *location).release();
      }
    };

#define PSI_MACRO_EVALUATE_CALLBACK_WRAPPER(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super) \
    &MacroEvaluateCallbackWrapper<derived>::evaluate \
  }

    class MacroDotCallback;

    /**
     * \see MacroEvaluateCallback
     */
    struct MacroDotCallbackVtable {
      TreeVtable base;
      Expression* (*dot) (MacroDotCallback*, Expression*, EvaluateContext*, const SourceLocation*);
    };

    /**
     * \brief Wrapper class to ease using MacroEvaluateCallbackVtable from C++.
     */
    class MacroDotCallback : public Tree {
      const MacroDotCallbackVtable* derived_vptr() {return reinterpret_cast<const MacroDotCallbackVtable*>(m_vptr);}
    public:
      static const MacroDotCallbackVtable vtable;

      TreePtr<Expression> dot(const TreePtr<Expression>& value,
                              const TreePtr<EvaluateContext>& evaluate_context,
                              const SourceLocation& location) {
        return TreePtr<Expression>(derived_vptr()->dot(this, value.get(), evaluate_context.get(), &location), false);
      }
    };

    /**
     * \brief Wrapper class to ease implementing MacroEvaluateCallbackVtable in C++.
     */
    template<typename Derived, typename Impl=Derived>
    struct MacroDotCallbackWrapper : NonConstructible {
      static Tree* dot(MacroDotCallback *self, Tree *value, EvaluateContext *evaluate_context, const SourceLocation *location) {
        return Impl::dot(*static_cast<Derived*>(self), TreePtr<>(value), TreePtr<EvaluateContext>(evaluate_context), *location).release();
      }
    };

#define PSI_MACRO_DOT_CALLBACK_WRAPPER(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super) \
    &MacroDotCallbackWrapper<derived>::dot \
  }

    struct InterfaceVtable {
      void (*name) (String*, Interface*);
    };

    class Interface : public Tree {
      const InterfaceVtable* derived_vptr() {return reinterpret_cast<const InterfaceVtable*>(m_vptr);}
    public:
      String name() {
        ResultStorage<String> result;
        derived_vptr()->name(result.ptr(), this);
        return result.done();
      }
    };

    class Block;

    TreePtr<Expression> compile_expression(const SharedPtr<Parser::Expression>&, const TreePtr<EvaluateContext>&, const SharedPtr<LogicalSourceLocation>&);
    TreePtr<Block> compile_statement_list(const List<SharedPtr<Parser::NamedExpression> >&, const TreePtr<EvaluateContext>&, const SourceLocation&);
    SharedPtr<LogicalSourceLocation> make_logical_location(const SharedPtr<LogicalSourceLocation>&, const String&);
    String logical_location_name(const SharedPtr<LogicalSourceLocation>& location);

    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<Expression> >&, const TreePtr<EvaluateContext>&);
    TreePtr<EvaluateContext> evaluate_context_dictionary(CompileContext&, const SourceLocation&, const std::map<String, TreePtr<Expression> >&);

    TreePtr<> interface_lookup(const TreePtr<Interface>&, const List<TreePtr<> >&, const SourceLocation&);
    TreePtr<> interface_lookup(const TreePtr<Interface>&, const TreePtr<>&, const SourceLocation&);
    void interface_cast_check(const TreePtr<Interface>&, const TreePtr<>&, const SourceLocation&, const TreeVtable*);

    template<typename T>
    TreePtr<T> interface_lookup_as(const TreePtr<Interface>& interface, const TreePtr<>& parameter, const SourceLocation& location) {
      TreePtr<> result = interface_lookup(interface, parameter, location);
      interface_cast_check(interface, result, location, reinterpret_cast<const TreeVtable*>(&T::vtable));
      return treeptr_cast<T>(result);
    }

    template<typename T>
    TreePtr<T> interface_lookup_as(const TreePtr<Interface>& interface, const List<TreePtr<> >& parameters, const SourceLocation& location) {
      TreePtr<> result = interface_lookup(interface, parameters, location);
      interface_cast_check(interface, result, location, reinterpret_cast<const TreeVtable*>(&T::vtable));
      return treeptr_cast<T>(result);
    }
    
    TreePtr<> function_definition_object(CompileContext&);
    
    TreePtr<GlobalTree> tree_from_address(CompileContext&, const SourceLocation&, const TreePtr<Type>&, void*);

    TreePtr<Macro> make_interface(CompileContext&, const SourceLocation&, const String&, const TreePtr<MacroEvaluateCallback>&, const std::map<String, TreePtr<MacroDotCallback> >&);
    TreePtr<Macro> make_interface(CompileContext&, const SourceLocation&, const String&, const TreePtr<MacroEvaluateCallback>&);
    TreePtr<Macro> make_interface(CompileContext&, const SourceLocation&, const String&, const std::map<String, TreePtr<MacroDotCallback> >&);
    TreePtr<Macro> make_interface(CompileContext&, const SourceLocation&, const String&);
  }
}

#endif
