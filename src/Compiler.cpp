#include "Compiler.hpp"
#include "TvmLowering.hpp"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>

#if PSI_HAVE_EXECINFO
#include <execinfo.h>
#endif

namespace Psi {
  namespace Compiler {
    bool si_derived(const SIVtable *base, const SIVtable *derived) {
      for (const SIVtable *super = derived; super; super = super->super) {
        PSI_ASSERT(super->super != derived);
        if (super == base)
          return true;
      }
      
      return false;
    }

    bool si_is_a(const SIBase *object, const SIVtable *cls) {
      return si_derived(cls, object->m_vptr);
    }

#if PSI_DEBUG
    std::set<void*> CompileContext::object_pointers() {
      std::set<void*> pointers;
      BOOST_FOREACH(Object& t, m_gc_list)
        pointers.insert(&t);
      return pointers;
    }
#endif

    CompileContext::CompileContext(CompileErrorContext *error_context, const PropertyValue& jit_configuration)
    : m_error_context(error_context),
    m_running_completion_stack(NULL),
    m_functional_term_buckets(initial_functional_term_buckets),
    m_functional_term_set(FunctionalTermSetType::bucket_traits(m_functional_term_buckets.get(), m_functional_term_buckets.size())),
    m_root_location(PhysicalSourceLocation(), LogicalSourceLocation::new_root()) {
      PSI_ASSERT(error_context);
      
#if PSI_OBJECT_PTR_DEBUG
      m_object_ptr_offset = 0;
#endif

      PhysicalSourceLocation core_physical_location;
      m_root_location.physical.file.reset(new SourceFile());
      m_root_location.physical.first_line = m_root_location.physical.first_column = 0;
      m_root_location.physical.last_line = m_root_location.physical.last_column = 0;
      m_builtins.initialize(*this);
      CompileErrorPair err_loc(*error_context, SourceLocation::root_location("(jit)"));
      m_jit = boost::make_shared<TvmJit>(boost::ref(*this), boost::ref(err_loc), jit_configuration);
    }
    
#if PSI_DEBUG
#define PSI_COMPILE_CONTEXT_REFERENCE_GUARD 100
#else
#define PSI_COMPILE_CONTEXT_REFERENCE_GUARD 1
#endif

    struct CompileContext::ObjectDisposer {
#if PSI_DEBUG
      bool force_destroy;
      ObjectDisposer(bool force_destroy_) : force_destroy(force_destroy_) {}
#endif

      void operator () (Object *t) {
#if PSI_DEBUG
        if (force_destroy || (t->m_reference_count == PSI_COMPILE_CONTEXT_REFERENCE_GUARD)) {
          t->m_reference_count = 0;
          derived_vptr(t)->destroy(t);
        }
#else
        derived_vptr(t)->destroy(t);
#endif
      }
    };

    CompileContext::~CompileContext() {
      m_builtins = BuiltinTypes();
      m_jit.reset();

      // Add extra reference to each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        t.m_reference_count += PSI_COMPILE_CONTEXT_REFERENCE_GUARD;
#if PSI_OBJECT_PTR_DEBUG
      m_object_ptr_offset = PSI_COMPILE_CONTEXT_REFERENCE_GUARD;
#endif

      // Clear cross references in each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        derived_vptr(&t)->gc_clear(&t);
        
#if PSI_DEBUG
      // Check for dangling references
      bool failed = false, force_destroy = false;
      for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii) {
        if (ii->m_reference_count != PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
          if (!failed) {
            failed = true;
            PSI_WARNING_FAIL("Incorrect reference count during context destruction");
          }
          
          const char *name = si_vptr(&*ii)->classname;
          if (ii->m_reference_count < PSI_COMPILE_CONTEXT_REFERENCE_GUARD)
            PSI_WARNING_FAIL2("Multiple release", name);
          else if (ii->m_reference_count > PSI_COMPILE_CONTEXT_REFERENCE_GUARD)
            PSI_WARNING_FAIL2("Dangling references", name);
        }
      }
      
      if (failed) {
        force_destroy = std::getenv("PSI_GC_FORCE_DESTROY");
        
        std::map<const char*, std::set<const char*> > suspects;
          
        // Try to destroy each object with zero remaining references and see what happens...
        GCListType queue;
        
        // Remove all objects with zero remaining references
        for (GCListType::iterator ii = m_gc_list.begin(), in, ie = m_gc_list.end(); ii != ie; ii = in) {
          in = ii; ++in;
          if (ii->m_reference_count == PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
            Object& obj = *ii;
            m_gc_list.erase(ii);
            queue.push_back(obj);
          }
        }
        
        std::vector<std::size_t>  reference_counts;
        unsigned by_force_count = 0;
        while(true) {
          while (!queue.empty()) {
            Object *obj = &queue.front();
            queue.pop_front();

            reference_counts.clear();
            for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii)
              reference_counts.push_back(ii->m_reference_count);

            const char *type = si_vptr(obj)->classname;
            obj->m_reference_count = 0;
            derived_vptr(obj)->destroy(obj);
            
            // If the reference count of any object has changed, gc_clear for this object has not worked
            std::vector<std::size_t>::const_iterator ref_it = reference_counts.begin();
            for (GCListType::iterator ii = m_gc_list.begin(), in = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ii = in, ++ref_it) {
              PSI_WARNING(ref_it != reference_counts.end());
              ++in;
              if (ii->m_reference_count != *ref_it)
                suspects[type].insert(si_vptr(&*ii)->classname);
              
              if (ii->m_reference_count == PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
                Object& obj2 = *ii;
                m_gc_list.erase(ii);
                queue.push_back(obj2);
              }
            }
            PSI_WARNING(ref_it == reference_counts.end());
          }
          
          if (force_destroy && !m_gc_list.empty()) {
            Object& obj = m_gc_list.front();
            std::cerr << "Destroying by force: " << si_vptr(&obj)->classname << ", refcount " << int(obj.m_reference_count - PSI_COMPILE_CONTEXT_REFERENCE_GUARD) << '\n';
            m_gc_list.erase(m_gc_list.begin());
            queue.push_back(obj);
            ++by_force_count;
          } else
            break;
        }
        
        if (by_force_count)
          std::cerr << "Destroyed " << by_force_count << " objects by force\n";
        
        std::cerr << "These types appear to have GC errors:\n";
        for (std::map<const char*, std::set<const char*> >::const_iterator ii = suspects.begin(), ie = suspects.end(); ii != ie; ++ii) {
          std::cerr << "Type: " << ii->first << '\n';
          for (std::set<const char*>::const_iterator ji = ii->second.begin(), je = ii->second.end(); ji != je; ++ji)
            std::cerr << "  still references: " << *ji << '\n';
        }
        
        // Print remaining objects
        unsigned refs_over = 0, refs_under = 0;
        std::cerr << "These have dangling references:\n";
        BOOST_FOREACH(Object& t, m_gc_list) {
          int ref_delta = t.m_reference_count - PSI_COMPILE_CONTEXT_REFERENCE_GUARD;
          if (ref_delta >= 0)
            refs_over += ref_delta;
          else
            refs_under -= ref_delta;
          std::cerr << t.m_vptr->classname << ' ' << ref_delta << '\n';
        }
        std::cerr << "Remaining object count: " << m_gc_list.size() << '\n';
        std::cerr << "Total references over: " << refs_over << ", under: " << refs_under << '\n';
      }
#endif

#if PSI_DEBUG
      m_gc_list.clear_and_dispose(ObjectDisposer(force_destroy));
#else
      m_gc_list.clear_and_dispose(ObjectDisposer());
#endif

      PSI_WARNING(m_functional_term_set.empty());
      
#if PSI_OBJECT_PTR_DEBUG
      std::cerr << m_object_ptr_set.size() << " surviving ObjectPtrs\n";
      for (ObjectPtrSetType::const_iterator ii = m_object_ptr_set.begin(), ie = m_object_ptr_set.end(); ii != ie; ++ii) {
        std::cerr << "ObjectPtr still exists at context destruction at " << ii->first << std::endl;
        object_ptr_backtrace(ii->second);
      }
#endif
    }
    
#if PSI_OBJECT_PTR_DEBUG
    void CompileContext::object_ptr_backtrace(const ObjectPtrSetValue& value) {
#if PSI_HAVE_EXECINFO
      unsigned n = 0;
      for (; (n < object_ptr_backtrace_depth) && value.backtrace[n]; ++n);
      backtrace_symbols_fd(value.backtrace, n, 2);
#endif
    }

    void CompileContext::object_ptr_add(const Object *obj, void *ptr) {
      ObjectPtrSetValue value;
      value.obj = obj;
      std::fill_n(value.backtrace, object_ptr_backtrace_depth, static_cast<void*>(NULL));
#if PSI_HAVE_EXECINFO
      backtrace(value.backtrace, object_ptr_backtrace_depth);
#endif
      
      std::pair<ObjectPtrSetType::iterator, bool> ins = m_object_ptr_set.insert(std::make_pair(ptr, value));
      if (!ins.second) {
        std::cerr << "ObjectPtr initialized a second time at the same address" << std::endl;
        object_ptr_backtrace(ins.first->second);
        ins.first->second = value;
      }
      
      std::size_t& aux_count = m_object_aux_count_map[obj];
      ++aux_count;
      if (obj->m_reference_count != aux_count * PSI_REFERENCE_COUNT_GRANULARITY + m_object_ptr_offset) {
        std::cerr << "Object reference count out of sync (inc) " << (aux_count * PSI_REFERENCE_COUNT_GRANULARITY + m_object_ptr_offset) << ' ' << obj->m_reference_count << std::endl;
      }
    }
    
    void CompileContext::object_ptr_remove(const Object *obj, void *ptr) {
      ObjectPtrSetType::iterator ii = m_object_ptr_set.find(ptr);
      if (ii == m_object_ptr_set.end()) {
        std::cerr << "Unknown object pointer destroyed\n";
      } else {
        if (ii->second.obj != obj) {
          std::cerr << "ObjectPtr removed with different object" << std::endl;
          object_ptr_backtrace(ii->second);
        }
        m_object_ptr_set.erase(ii);
        
        std::size_t& aux_count = m_object_aux_count_map[obj];
        if (obj->m_reference_count != aux_count * PSI_REFERENCE_COUNT_GRANULARITY + m_object_ptr_offset) {
          std::cerr << "Object reference count out of sync (dec) " << (aux_count * PSI_REFERENCE_COUNT_GRANULARITY + m_object_ptr_offset) << ' ' << obj->m_reference_count << std::endl;
        }
        --aux_count;
      }
    }

    void CompileContext::object_ptr_move(const Object *obj, void *from, void *to) {
      object_ptr_remove(obj, from);
      object_ptr_add(obj, to);
    }
#endif

    /**
     * \brief JIT compile a global variable or function.
     */
    void* CompileContext::jit_compile(const TreePtr<Global>& global) {
      return m_jit->jit_compiler().compile(global);
    }
    
    /**
     * \brief Compile many globals at once using the JIT.
     */
    void CompileContext::jit_compile_many(const PSI_STD::vector<TreePtr<Global> >& globals) {
      m_jit->jit_compiler().jit_compile(globals);
    }
    
    namespace {
      struct FunctionalEqualsData {
        std::size_t hash;
        const SIVtable *vptr;
        const Functional *value;
      };

      struct FunctionalSetupHasher {
        std::size_t operator () (const FunctionalEqualsData& arg) const {
          return arg.hash;
        }
      };
    }
    
    struct CompileContext::FunctionalSetupEquals {
      bool operator () (const FunctionalEqualsData& lhs, const Functional& rhs) const {
        if (lhs.hash != rhs.m_hash)
          return false;
        if (lhs.vptr != si_vptr(&rhs))
          return false;
        return lhs.value->equivalent(rhs);
      }
    };

    TreePtr<Functional> CompileContext::get_functional_ptr(const Functional& value, const SourceLocation& location) {
      PSI_ASSERT(value.m_reference_count == 0);
      FunctionalEqualsData data;
      data.hash = value.compute_hash();
      data.vptr = si_vptr(&value);
      data.value = &value;

      FunctionalTermSetType::insert_commit_data commit_data;
      std::pair<FunctionalTermSetType::iterator, bool> r = m_functional_term_set.insert_check(data, FunctionalSetupHasher(), FunctionalSetupEquals(), commit_data);
      if (!r.second)
        return TreePtr<Functional>(&*r.first);

      Functional *result_ptr = value.clone();
      m_gc_list.push_back(*result_ptr);
      result_ptr->m_compile_context = this; // This must come before TreePtr<> construction in order for PSI_OBJECT_PTR_DEBUG to work
      PSI_ASSERT(result_ptr->m_reference_count == 0);
      TreePtr<Functional> result(result_ptr);

      result_ptr->m_hash = data.hash;
      result_ptr->m_location = location;
      TermResultInfo tri = result_ptr->check_type();
      result_ptr->type = tri.type;
      result_ptr->pure = tri.pure;
      result_ptr->mode = tri.mode;
      
      m_functional_term_set.insert_commit(*result_ptr, commit_data);
      
      if (m_functional_term_set.size() >= m_functional_term_set.bucket_count()) {
        UniqueArray<FunctionalTermSetType::bucket_type> new_buckets(m_functional_term_set.bucket_count() * 2);
        m_functional_term_set.rehash(FunctionalTermSetType::bucket_traits(new_buckets.get(), new_buckets.size()));
        swap(new_buckets, m_functional_term_buckets);
      }
      
      return result;
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
      
      static void overload_list_impl(const EvaluateContextDictionary& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.next)
          self.next->overload_list(overload_type, overload_list);
      }
    };

    const EvaluateContextVtable EvaluateContextDictionary::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextDictionary, "psi.compiler.EvaluateContextDictionary", EvaluateContext);

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>& module, const SourceLocation& location, const std::map<String, TreePtr<Term> >& entries, const TreePtr<EvaluateContext>& next) {
      return TreePtr<EvaluateContext>(::new EvaluateContextDictionary(module, location, entries, next));
    }

    /**
     * \brief Create an evaluation context based on a dictionary.
     */
    TreePtr<EvaluateContext> evaluate_context_dictionary(const TreePtr<Module>& module, const SourceLocation& location, const std::map<String, TreePtr<Term> >& entries) {
      return evaluate_context_dictionary(module, location, entries, TreePtr<EvaluateContext>());
    }

    TreePtr<EvaluateContext> evaluate_context_dictionary(const SourceLocation& location, const std::map<String, TreePtr<Term> >& names, const TreePtr<EvaluateContext>& parent) {
      return evaluate_context_dictionary(parent->module(), location, names, parent);
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
      
      static void overload_list_impl(const EvaluateContextModule& self, const TreePtr<OverloadType>& overload_type,
                                     PSI_STD::vector<TreePtr<OverloadValue> >& overload_list) {
        if (self.next)
          self.next->overload_list(overload_type, overload_list);
      }
    };

    const EvaluateContextVtable EvaluateContextModule::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextModule, "psi.compiler.EvaluateContextModule", EvaluateContext);

    /**
     * \brief Evaluate context which changes target module but forwards name lookups.
     */
    TreePtr<EvaluateContext> evaluate_context_module(const TreePtr<Module>& module, const TreePtr<EvaluateContext>& next, const SourceLocation& location) {
      return TreePtr<EvaluateContext>(::new EvaluateContextModule(module, next, location));
    }
  }
}
