#include "Compiler.hpp"
#include "Tree.hpp"
#include "Platform.hpp"

#include "Class.hpp"
#include "Function.hpp"

#ifdef PSI_DEBUG
#include <iostream>
#endif

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    void TreePtrBase::update_chain(const TreeBase *ptr) const {
      const TreePtrBase *hook = this;
      ObjectPtr<const TreeCallback> ptr_cb;
      while (hook->m_ptr.get() != ptr) {
        PSI_ASSERT(derived_vptr(hook->m_ptr.get())->is_callback);
        ObjectPtr<const TreeCallback> next_ptr_cb(static_cast<const TreeCallback*>(hook->m_ptr.get()), true);
        const TreePtrBase *next_hook = &next_ptr_cb->m_value;
        hook->m_ptr.reset(ptr);
        hook = next_hook;
        ptr_cb.swap(next_ptr_cb);
      }
    }
    
    /**
     * Evaluate a lazily evaluated Tree (recursively if necessary) and return the final result.
     */
    const Tree* TreePtrBase::get_helper() const {
      PSI_ASSERT(m_ptr);

      /*
       * Evaluate chain of hooks until either a NULL is found or a non-callback
       * value is reached.
       */
      const TreePtrBase *hook = this;
      while (true) {
        if (!hook->m_ptr)
          break;
        
        const TreeBaseVtable *vtable = derived_vptr(hook->m_ptr.get());
        if (!vtable->is_callback)
          break;

        TreeCallback *ptr_cb = static_cast<TreeCallback*>(const_cast<TreeBase*>(hook->m_ptr.get()));
        hook = &ptr_cb->m_value;

        switch (ptr_cb->m_state) {
        case TreeCallback::state_ready: {
          const TreeCallbackVtable *vtable_cb = reinterpret_cast<const TreeCallbackVtable*>(vtable);
          RunningTreeCallback running(ptr_cb);
          ptr_cb->m_state = TreeCallback::state_running;
          const TreeBase *eval_ptr;
          try {
            eval_ptr = vtable_cb->evaluate(ptr_cb);
          } catch (...) {
            ptr_cb->m_state = TreeCallback::state_failed;
            update_chain(ptr_cb);
            throw;
          }
          PSI_ASSERT(!hook->m_ptr);
          hook->m_ptr.reset(eval_ptr, false);
          ptr_cb->m_state = TreeCallback::state_finished;
          break;
        }

        case TreeCallback::state_running:
          update_chain(ptr_cb);
          RunningTreeCallback::throw_circular_dependency(ptr_cb);
          PSI_FAIL("Previous line should have thrown an exception");

        case TreeCallback::state_finished:
          break;

        case TreeCallback::state_failed:
          update_chain(ptr_cb);
          throw CompileException();
        }
      }

      update_chain(hook->m_ptr.get());

      PSI_ASSERT(!m_ptr || !derived_vptr(m_ptr.get())->is_callback);
      return static_cast<const Tree*>(m_ptr.get());
    }
    
#ifdef PSI_DEBUG
    void TreePtrBase::debug_print() const {
      if (!m_ptr) {
        std::cerr << "(null)" << std::endl;
        return;
      }
      
      const SourceLocation& loc = location();
      std::cerr << loc.physical.file->url << ':' << loc.physical.first_line << ": "
      << loc.logical->error_name(LogicalSourceLocationPtr())
      << " : " << si_vptr(m_ptr.get())->classname << std::endl;
    }
#endif    

    bool si_derived(const SIVtable *base, const SIVtable *derived) {
      for (const SIVtable *super = derived; super; super = super->super) {
        if (super == base)
          return true;
      }
      
      return false;
    }

    bool si_is_a(const SIBase *object, const SIVtable *cls) {
      return si_derived(cls, object->m_vptr);
    }

    /**
     * Create a string containing a list of parameters passed to an interface.
     */
    std::string interface_parameters_message(const List<TreePtr<Term> >& parameters, const SourceLocation& location) {
      std::stringstream ss;

      bool first = true;
      for (LocalIterator<TreePtr<Term> > p(parameters); p.next();) {
	      if (first)
	        first = false;
	      else
	        ss << ", ";
        TreePtr<Term>& current = p.current();
	      ss << '\'' << current.location().logical->error_name(location.logical) << '\'';
      }

      return ss.str();
    }

    /**
     * \brief Checks the result of an interface lookup is non-NULL and of the correct type.
     *
     * \param parameters Parameters the interfacce was searched on.
     */
    void interface_cast_check(const TreePtr<Interface>& interface, const List<TreePtr<Term> >& parameters, const TreePtr<>& result, const SourceLocation& location, const TreeVtable* cast_type) {
      CompileContext& compile_context = interface.compile_context();

      if (!result)
        compile_context.error_throw(location,
                                    boost::format("'%s' interface not available for %s")
                                    % interface.location().logical->error_name(location.logical)
                                    % interface_parameters_message(parameters, location));

      if (!si_is_a(result.get(), &cast_type->base.base.base))
        compile_context.error_throw(location,
                                    boost::format("'%s' interface value has the wrong type (%s) for %s")
                                    % interface.location().logical->error_name(location.logical)
                                    % si_vptr(result.get())->classname
                                    % interface_parameters_message(parameters, location),
                                    CompileError::error_internal);
    }

    CompileException::CompileException() {
    }

    CompileException::~CompileException() throw() {
    }

    const char *CompileException::what() const throw() {
      return "Psi compile exception";
    }

    CompileError::CompileError(CompileContext& compile_context, const SourceLocation& location, unsigned flags)
      : m_compile_context(&compile_context), m_location(location), m_flags(flags) {
      bool error_occurred = false;
      switch (flags) {
      case error_warning: m_type = "warning"; break;
      case error_internal: m_type = "internal error"; error_occurred = true; break;
      default: m_type = "error"; error_occurred = true; break;
      }

      if (error_occurred)
        m_compile_context->set_error_occurred();

      m_compile_context->error_stream() << boost::format("%s:%s: in '%s'\n") % location.physical.file->url
        % location.physical.first_line % location.logical->error_name(LogicalSourceLocationPtr(), true);
    }

    void CompileError::info(const std::string& message) {
      info(m_location, message);
    }

    void CompileError::info(const SourceLocation& location, const std::string& message) {
      m_compile_context->error_stream() << boost::format("%s:%s:%s: %s\n")
        % location.physical.file->url % location.physical.first_line % m_type % message;
    }

    void CompileError::end() {
    }
    
    BuiltinTypes::BuiltinTypes() {
    }

    void BuiltinTypes::initialize(CompileContext& compile_context) {
      SourceLocation psi_location = compile_context.root_location().named_child("psi");
      SourceLocation psi_compiler_location = psi_location.named_child("compiler");

      metatype.reset(new Metatype(compile_context, psi_location.named_child("Type")));
      empty_type.reset(new EmptyType(compile_context, psi_location.named_child("Empty")));
      bottom_type.reset(new BottomType(compile_context, psi_location.named_child("Bottom")));
      
#if 0
      macro_interface.reset(new MetadataType(compile_context, 1, &Macro::vtable, psi_compiler_location.named_child("Macro")));
      argument_passing_info_interface.reset(new MetadataType(compile_context, 1, &ArgumentPassingInfoCallback::vtable, psi_compiler_location.named_child("ArgumentPasser")));
      return_passing_info_interface.reset(new MetadataType(compile_context, 1, &ReturnPassingInfoCallback::vtable, psi_compiler_location.named_child("ReturnMode")));
      class_member_info_interface.reset(new MetadataType(compile_context, 1, &ClassMemberInfoCallback::vtable, psi_compiler_location.named_child("ClassMemberInfo")));
#else
      PSI_NOT_IMPLEMENTED();
#endif
    }

    CompileContext::CompileContext(std::ostream *error_stream)
    : m_error_stream(error_stream), m_error_occurred(false), m_running_completion_stack(NULL),
    m_root_location(PhysicalSourceLocation(), LogicalSourceLocation::new_root_location()) {
      PhysicalSourceLocation core_physical_location;
      m_root_location.physical.file.reset(new SourceFile());
      m_root_location.physical.first_line = m_root_location.physical.first_column = 0;
      m_root_location.physical.last_line = m_root_location.physical.last_column = 0;
      m_builtins.initialize(*this);
    }
    
#ifdef PSI_DEBUG
#define PSI_COMPILE_CONTEXT_REFERENCE_GUARD 20
#else
#define PSI_COMPILE_CONTEXT_REFERENCE_GUARD 1
#endif

    struct CompileContext::ObjectDisposer {
      void operator () (Object *t) {
#ifdef PSI_DEBUG
        if (t->m_reference_count == PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
          t->m_reference_count = 0;
          derived_vptr(t)->destroy(t);
        } else if (t->m_reference_count < PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
          PSI_WARNING_FAIL("Reference counting error: guard references have been used up");
          PSI_WARNING_FAIL(t->m_vptr->classname);
        } else {
          PSI_WARNING_FAIL("Reference counting error: dangling references to object");
          PSI_WARNING_FAIL(t->m_vptr->classname);
        }
#else
        derived_vptr(t)->destroy(t);
#endif
      }
    };

    CompileContext::~CompileContext() {
      m_builtins = BuiltinTypes();

      // Add extra reference to each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        t.m_reference_count += PSI_COMPILE_CONTEXT_REFERENCE_GUARD;

      // Clear cross references in each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        derived_vptr(&t)->gc_clear(&t);
        
#ifdef PSI_DEBUG
      bool failed = false;
      for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii) {
        if (ii->m_reference_count != PSI_COMPILE_CONTEXT_REFERENCE_GUARD)
          failed = true;
      }
      
      if (failed) {
        PSI_WARNING_FAIL("Incorrect reference count during context destruction: either dangling reference or multiple release");
        BOOST_FOREACH(Object& t, m_gc_list)
          PSI_WARNING_FAIL(t.m_vptr->classname);
      }
#endif

      m_gc_list.clear_and_dispose(ObjectDisposer());
    }

    void CompileContext::error(const SourceLocation& loc, const std::string& message, unsigned flags) {
      CompileError error(*this, loc, flags);
      error.info(message);
      error.end();
    }

    void CompileContext::error_throw(const SourceLocation& loc, const std::string& message, unsigned flags) {
      error(loc, message, flags);
      throw CompileException();
    }

    /**
     * \brief JIT compile a global variable or function.
     */
    void* CompileContext::jit_compile(const TreePtr<Global>& global) {
      PSI_NOT_IMPLEMENTED();
    }

    RunningTreeCallback::RunningTreeCallback(TreeCallback *callback)
      : m_callback(callback) {
      m_parent = callback->compile_context().m_running_completion_stack;
      callback->compile_context().m_running_completion_stack = this;
    }

    RunningTreeCallback::~RunningTreeCallback() {
      m_callback->compile_context().m_running_completion_stack = m_parent;
    }

    /**
     * \brief Throw a circular dependency error caused by something depending on
     * its own value for evaluation.
     * 
     * \param callback Callback being recursively evaluated.
     */
    void RunningTreeCallback::throw_circular_dependency(TreeCallback *callback) {
      PSI_ASSERT(callback->m_state == TreeCallback::state_running);
      CompileError error(callback->compile_context(), callback->m_location);
      error.info("Circular dependency found");
      boost::format fmt("via: '%s'");
      for (RunningTreeCallback *ancestor = callback->compile_context().m_running_completion_stack;
           ancestor && (ancestor->m_callback != callback); ancestor = ancestor->m_parent)
        error.info(ancestor->m_callback->m_location, fmt % ancestor->m_callback->m_location.logical->error_name(callback->m_location.logical));
      error.end();
      throw CompileException();
    }
    
    class EvaluateContextDictionary : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef PSI_STD::map<String, TreePtr<Term> > NameMapType;

      EvaluateContextDictionary(const TreePtr<Module>& module,
                                const SourceLocation& location,
                                const NameMapType& entries_,
                                const TreePtr<EvaluateContext>& next_)
      : EvaluateContext(&vtable, module, location), entries(entries_), next(next_) {
      }

      NameMapType entries;
      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("entries", &EvaluateContextDictionary::entries)
        ("next", &EvaluateContextDictionary::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const EvaluateContextDictionary& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        NameMapType::const_iterator it = self.entries.find(name);
        if (it != self.entries.end()) {
          return lookup_result_match(it->second);
        } else if (self.next) {
          return self.next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable EvaluateContextDictionary::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextDictionary, "psi.compiler.EvaluateContextDictionary", EvaluateContext);

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>& module, const SourceLocation& location, const std::map<String, TreePtr<Term> >& entries, const TreePtr<EvaluateContext>& next) {
      return TreePtr<EvaluateContext>(new EvaluateContextDictionary(module, location, entries, next));
    }

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>& module, const SourceLocation& location, const std::map<String, TreePtr<Term> >& entries) {
      return evaluate_context_dictionary(module, location, entries, TreePtr<EvaluateContext>());
    }

    class EvaluateContextModule : public EvaluateContext {
    public:
      static const EvaluateContextVtable vtable;

      typedef PSI_STD::map<String, TreePtr<Term> > NameMapType;

      EvaluateContextModule(const TreePtr<Module>& module,
                            const TreePtr<EvaluateContext>& next_,
                            const SourceLocation& location)
      : EvaluateContext(&vtable, module, location), next(next_) {
      }

      TreePtr<EvaluateContext> next;

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("next", &EvaluateContextModule::next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const EvaluateContextModule& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        return self.next->lookup(name, location, evaluate_context);
      }
    };

    const EvaluateContextVtable EvaluateContextModule::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextModule, "psi.compiler.EvaluateContextModule", EvaluateContext);

    /**
     * \brief Evaluate context which changes target module but forwards name lookups.
     */
    TreePtr<EvaluateContext> evaluate_context_module(const TreePtr<Module>& module, const TreePtr<EvaluateContext>& next, const SourceLocation& location) {
      return TreePtr<EvaluateContext>(new EvaluateContextModule(module, next, location));
    }
    
    /**
     * \brief Find a global or function by name inside a namespace tree.
     * 
     * \param ns Namespace under which to search for a function.
     */
    TreePtr<Term> find_by_name(const TreePtr<Namespace>& ns, const std::string& name) {
      std::size_t dot_pos = name.find('.');
      std::string prefix = name.substr(0, dot_pos);
      std::string suffix;
      if (dot_pos != std::string::npos)
        suffix = name.substr(dot_pos + 1);
      
      LogicalSourceLocationPtr ns_loc = ns->location().logical;
      BOOST_FOREACH(const TreePtr<Statement>& st, ns->statements) {
        const LogicalSourceLocationPtr& st_loc = st->location().logical;
        if (!st_loc->anonymous() &&
          (st_loc->parent() == ns_loc) &&
          (st_loc->name() == prefix)) {
          if (dot_pos == std::string::npos) {
            return st->value;
          } else if (TreePtr<Namespace> ns_child = dyn_treeptr_cast<Namespace>(st->value)) {
            if (TreePtr<Term> value = find_by_name(ns_child, suffix))
              return value;
          }
        }
      }
      
      return TreePtr<Term>();
    }
  }
}
