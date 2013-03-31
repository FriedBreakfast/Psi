#include "TreeBase.hpp"
#include "Compiler.hpp"

#include <boost/format.hpp>

#if PSI_DEBUG
#include <iostream>
#endif

namespace Psi {
  namespace Compiler {
    RunningTreeCallback::RunningTreeCallback(DelayedEvaluation *callback)
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
    void RunningTreeCallback::throw_circular_dependency() {
      CompileError error(m_callback->compile_context().error_context(), m_callback->location());
      error.info("Circular dependency found");
      boost::format fmt("via: '%s'");
      for (RunningTreeCallback *ancestor = m_callback->compile_context().m_running_completion_stack; ancestor; ancestor = ancestor->m_parent) {
        error.info(ancestor->m_callback->location(), fmt % ancestor->m_callback->location().logical->error_name(m_callback->location().logical));
        if (ancestor == this)
          break;
      }
      error.end();
      throw CompileException();
    }

    DelayedEvaluation::DelayedEvaluation(const DelayedEvaluationVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Object(PSI_COMPILER_VPTR_UP(Object, vptr), compile_context), m_location(location), m_state(state_ready) {
    }
    
    /**
     * Evaluate a delayed evaluation tree.
     */
    void DelayedEvaluation::evaluate(void *ptr, void *arg) {
      switch (m_state) {
      case state_ready: {
        RunningTreeCallback running(this);
        m_state = state_running;
        try {
          derived_vptr(this)->evaluate(ptr, this, arg);
        } catch (...) {
          m_state = state_failed;
          throw;
        }
        m_state = state_finished;
        return;
      }

      case state_running:
        RunningTreeCallback(this).throw_circular_dependency();
        PSI_FAIL("Previous line should have thrown an exception");

      case state_finished:
        compile_context().error_throw(m_location, "Delayed evaluation tree evaluated a second time", CompileError::error_internal);
        PSI_FAIL("Previous line should have thrown an exception");

      case state_failed:
        compile_context().error_throw(m_location, "Delayed evaluation tree previously failed", CompileError::error_internal);
        PSI_FAIL("Previous line should have thrown an exception");
      }
    }

#if PSI_DEBUG
    void Tree::debug_print() const {
      if (!this) {
        std::cerr << "(null)" << std::endl;
        return;
      }
      
      const SourceLocation& loc = location();
      std::cerr << loc.physical.file->url << ':' << loc.physical.first_line << ": "
      << loc.logical->error_name(LogicalSourceLocationPtr())
      << " : " << si_vptr(this)->classname << std::endl;
    }
#endif

    /**
     * Overload of object constructor which does not insert the object into a contexts
     * linked list. This is used by FunctionalTerm and its derived types only.
     */
    Object::Object(const ObjectVtable *vtable)
    : m_reference_count(0),
    m_compile_context(NULL) {
      PSI_COMPILER_SI_INIT(vtable);
      PSI_ASSERT(!m_vptr->abstract);
    }

    Object::Object(const ObjectVtable *vtable, CompileContext& compile_context)
    : m_reference_count(0),
    m_compile_context(&compile_context) {
      PSI_COMPILER_SI_INIT(vtable);
      PSI_ASSERT(!m_vptr->abstract);
      m_compile_context->m_gc_list.push_back(*this);
    }

    Object::Object(const Object& src)
    : m_reference_count(0),
    m_compile_context(NULL) {
      PSI_COMPILER_SI_INIT(src.m_vptr);
    }

    Object::~Object() {
      if (is_linked())
        m_compile_context->m_gc_list.erase(m_compile_context->m_gc_list.iterator_to(*this));
    }
    
    const SIVtable Object::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Object", NULL);

    /// \copydoc Object::Object(const ObjectVtable*)
    Tree::Tree(const TreeVtable *vptr)
    : Object(PSI_COMPILER_VPTR_UP(Object, vptr)) {
    }

    Tree::Tree(const TreeVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Object(PSI_COMPILER_VPTR_UP(Object, vptr), compile_context),
    m_location(location) {
    }
    
    /**
     * Recursively evaluate all tree references inside this tree.
     */
    void Tree::complete() const {
      VisitQueue<TreePtr<> > queue;
      queue.push(TreePtr<>(this));
      
      while (!queue.empty()) {
        TreePtr<> p = queue.pop();
        const Tree *ptr = p.get();
        derived_vptr(ptr)->complete(ptr, &queue);
      }
    }

    const SIVtable Tree::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Tree", &Object::vtable);
  }
}
