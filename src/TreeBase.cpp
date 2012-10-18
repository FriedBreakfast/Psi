#include "TreeBase.hpp"
#include "Compiler.hpp"

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
     * Evaluate a lazily evaluated Tree (recursively if necessary).
     * 
     * \param vptr If non-NULL, this indicates that a cast is being attempted, and we only need
     * evaluate as far as required to establish whether the cast is correct or not.
     * 
     * \return True if fully evaluated, false if only partially evaluated, which requires
     * that \c vptr is not null.
     */
    bool TreePtrBase::evaluate(const TreeVtable *vptr) const {
      PSI_ASSERT(m_ptr);
      bool full_eval = true;

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
        
        if (vptr && si_derived(reinterpret_cast<const SIVtable*>(vptr), reinterpret_cast<const SIVtable*>(ptr_cb->m_result_vptr))) {
          full_eval = false;
          break;
        }

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
      return full_eval;
    }
    
    /**
     * Evaluate a lazily evaluated Tree (recursively if necessary) and return the final result.
     */
    const Tree* TreePtrBase::get_helper() const {
      evaluate(NULL);
      PSI_ASSERT(!m_ptr || !derived_vptr(m_ptr.get())->is_callback);
      return static_cast<const Tree*>(m_ptr.get());
    }
    
    /**
     * \brief Check whether a Tree can be cast to the given type.
     * 
     * This may or may not fully evaluate this tree, depending on whether the evaluation
     * functions specify a more specific type or not. Note that a NULL value is counted
     * as castable to any type.
     */
    bool TreePtrBase::is_a(const TreeVtable *vptr) const {
      if (derived_vptr(m_ptr.get())->is_callback)
        if (!evaluate(vptr))
          return true;
      
      PSI_ASSERT(!m_ptr || !derived_vptr(m_ptr.get())->is_callback);
      return !m_ptr || si_derived(reinterpret_cast<const SIVtable*>(vptr), si_vptr(m_ptr.get()));
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

    Object::Object(const ObjectVtable *vtable, CompileContext& compile_context)
    : m_reference_count(0),
    m_compile_context(&compile_context) {
      PSI_COMPILER_SI_INIT(vtable);
      PSI_ASSERT(!m_vptr->abstract);
      m_compile_context->m_gc_list.push_back(*this);
    }

    Object::~Object() {
      if (is_linked())
        m_compile_context->m_gc_list.erase(m_compile_context->m_gc_list.iterator_to(*this));
    }
    
    const SIVtable Object::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Object", NULL);

    TreeBase::TreeBase(const TreeBaseVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : Object(PSI_COMPILER_VPTR_UP(Object, vptr), compile_context),
    m_location(location) {
    }
    
    const SIVtable TreeBase::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeBase", &Object::vtable);

    Tree::Tree(const TreeVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
    : TreeBase(PSI_COMPILER_VPTR_UP(TreeBase, vptr), compile_context, location) {
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

    const SIVtable Tree::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.Tree", &TreeBase::vtable);

    TreeCallback::TreeCallback(const TreeCallbackVtable *vptr, CompileContext& compile_context, const TreeVtable *result_vptr, const SourceLocation& location)
    : TreeBase(PSI_COMPILER_VPTR_UP(TreeBase, vptr), compile_context, location), m_state(state_ready), m_result_vptr(result_vptr) {
    }

    const SIVtable TreeCallback::vtable = PSI_COMPILER_SI_ABSTRACT("psi.compiler.TreeCallback", &TreeBase::vtable);
  }
}
