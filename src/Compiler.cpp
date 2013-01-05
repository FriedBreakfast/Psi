#include "Compiler.hpp"
#include "Tree.hpp"
#include "Platform.hpp"
#include "Macros.hpp"
#include "TvmLowering.hpp"

#ifdef PSI_DEBUG
#include <iostream>
#include <cstdlib>
#include "GCChecker.h"
#endif

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>

#ifdef PSI_DEBUG
extern "C" {
size_t psi_gcchecker_blocks(psi_gcchecker_block **ptr) __attribute__((weak));

size_t psi_gcchecker_blocks(psi_gcchecker_block **ptr) {
  *ptr = 0;
  return 0;
}
}
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
    
    namespace {
      class MakeTagCallback {
        SIType m_value_type;
        TreePtr<Term> m_wildcard_type;
        TreePtr<> m_default_value;
        
      public:
        typedef MetadataType TreeResultType;
        
        MakeTagCallback(const SIType& value_type, const TreePtr<Term>& wildcard_type, const TreePtr<>& default_value)
        : m_value_type(value_type), m_wildcard_type(wildcard_type), m_default_value(default_value) {
        }
        
        TreePtr<MetadataType> evaluate(const TreePtr<MetadataType>& self) {
          TreePtr<Term> wildcard(new Parameter(m_wildcard_type, 0, 0, self.location()));
          PSI_STD::vector<TreePtr<Term> > pattern(1, wildcard);
          PSI_STD::vector<TreePtr<Metadata> > values;
          if (m_default_value) {
            TreePtr<Term> impl_wildcard_1(new Parameter(self.compile_context().builtins().metatype, 0, 0, self.location()));
            TreePtr<Term> impl_wildcard_2(new Parameter(impl_wildcard_1, 0, 1, self.location()));
            PSI_STD::vector<TreePtr<Term> > impl_pattern(1, impl_wildcard_2);
            values.push_back(TreePtr<Metadata>(new Metadata(m_default_value, self, 2, impl_pattern, self.location())));
          }
          return TreePtr<MetadataType>(new MetadataType(self.compile_context(), 0, pattern, values, m_value_type, self.location()));
        }
        
        template<typename V>
        static void visit(V& v) {
          v("value_type", &MakeTagCallback::m_value_type)
          ("wildcard_type", &MakeTagCallback::m_wildcard_type)
          ("default_value", &MakeTagCallback::m_default_value);
        }
      };
      
      template<typename TreeType>
      TreePtr<MetadataType> make_tag(const TreePtr<Term>& wildcard_type, const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        return tree_callback(wildcard_type.compile_context(), location, MakeTagCallback(reinterpret_cast<const SIVtable*>(&TreeType::vtable), wildcard_type, default_value));
      }
    }

    void BuiltinTypes::initialize(CompileContext& compile_context) {
      SourceLocation psi_location = compile_context.root_location().named_child("psi");
      SourceLocation psi_compiler_location = psi_location.named_child("compiler");

      metatype.reset(new Metatype(compile_context, psi_location.named_child("Type")));
      empty_type.reset(new EmptyType(compile_context, psi_location.named_child("Empty")));
      bottom_type.reset(new BottomType(compile_context, psi_location.named_child("Bottom")));
      
      SourceLocation macro_location = psi_compiler_location.named_child("Macro");
      macro_tag = make_tag<Macro>(metatype, macro_location, default_macro_impl(compile_context, macro_location));
      library_tag = make_tag<Library>(metatype, psi_compiler_location.named_child("Library"));
      namespace_tag = make_tag<Namespace>(metatype, psi_compiler_location.named_child("Namespace"));
    }

    CompileContext::CompileContext(std::ostream *error_stream)
    : m_error_stream(error_stream), m_error_occurred(false), m_running_completion_stack(NULL),
    m_root_location(PhysicalSourceLocation(), LogicalSourceLocation::new_root_location()) {
      PhysicalSourceLocation core_physical_location;
      m_root_location.physical.file.reset(new SourceFile());
      m_root_location.physical.first_line = m_root_location.physical.first_column = 0;
      m_root_location.physical.last_line = m_root_location.physical.last_column = 0;
      m_builtins.initialize(*this);
      m_tvm_compiler.reset(new TvmCompiler(this));
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
    
#ifdef PSI_DEBUG
    struct MemoryBlockData {
      std::size_t size;
      Object *object;
      bool owned;
      
      MemoryBlockData(std::size_t n) : size(n), object(NULL), owned(false) {}
    };

    typedef std::map<void*, MemoryBlockData> MemoryBlockMap;
    
    void scan_block(const char *type, void *base, size_t size,
                    std::map<void*,MemoryBlockData>& map, std::set<const char*>& suspects) {
      char **ptrs = reinterpret_cast<char**>(base);
      for (std::size_t count = size / sizeof(void*); count; --count, ++ptrs) {
        MemoryBlockMap::iterator ii = map.find(*ptrs);
        if (ii != map.end()) {
          if (ii->second.object) {
            suspects.insert(type);
          } else if (!ii->second.owned) {
            ii->second.owned = true;
            scan_block(type, ii->first, ii->second.size, map, suspects);
          }
        }
      }
    }
#endif

    CompileContext::~CompileContext() {
      m_builtins = BuiltinTypes();
      m_tvm_compiler.reset();

      // Add extra reference to each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        t.m_reference_count += PSI_COMPILE_CONTEXT_REFERENCE_GUARD;

      // Clear cross references in each Tree
      BOOST_FOREACH(Object& t, m_gc_list)
        derived_vptr(&t)->gc_clear(&t);
        
#ifdef PSI_DEBUG
      // Check for dangling references
      bool failed = false;
      for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii) {
        if (ii->m_reference_count != PSI_COMPILE_CONTEXT_REFERENCE_GUARD)
          failed = true;
      }
      
      if (failed) {
        PSI_WARNING_FAIL("Incorrect reference count during context destruction: either dangling reference or multiple release");
        
        psi_gcchecker_block *blocks;
        size_t n_blocks = psi_gcchecker_blocks(&blocks);
        if (blocks) {
          std::set<const char*> suspects;
          
          // Construct a map of allocated blocks, and try and guess type which is not properly collected
          std::map<void*,MemoryBlockData> block_map;
          for (size_t i = 0; i != n_blocks; ++i)
            block_map.insert(std::make_pair(blocks[i].base, MemoryBlockData(blocks[i].size)));
          std::free(blocks);
          
          // Identify block each object belongs to
          BOOST_FOREACH(Object& t, m_gc_list) {
            MemoryBlockMap::iterator it = block_map.find(&t);
            if (it != block_map.end()) {
              it->second.object = &t;
              it->second.owned = true;
            }
          }
          
          for (MemoryBlockMap::const_iterator ii = block_map.begin(), ie = block_map.end(); ii != ie; ++ii) {
            if (ii->second.object)
              scan_block(ii->second.object->m_vptr->classname, ii->first, ii->second.size, block_map, suspects);
          }
        
          BOOST_FOREACH(const char *s, suspects)
            PSI_WARNING_FAIL(s);
        } else {
          std::set<const char*> suspects;
          
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
          
          while (!queue.empty()) {
            Object *obj = &queue.front();
            queue.pop_front();

            const char *type = si_vptr(obj)->classname;
            obj->m_reference_count = 0;
            derived_vptr(obj)->destroy(obj);
            
            // If any objects now have zero reference count, hold this object responsible!
            bool responsible = false;
            for (GCListType::iterator ii = m_gc_list.begin(), in, ie = m_gc_list.end(); ii != ie; ii = in) {
              in = ii; ++in;
              if (ii->m_reference_count == PSI_COMPILE_CONTEXT_REFERENCE_GUARD) {
                Object& obj2 = *ii;
                m_gc_list.erase(ii);
                queue.push_back(obj2);
                responsible = true;
              }
            }
            
            if (responsible)
              suspects.insert(type);
          }
          
          PSI_WARNING_FAIL("These types appear to have GC errors:");
          BOOST_FOREACH(const char *s, suspects)
            PSI_WARNING_FAIL(s);
          
          // Print remaining objects
          PSI_WARNING_FAIL("These types form a cycle:");
          BOOST_FOREACH(Object& t, m_gc_list)
            PSI_WARNING_FAIL(t.m_vptr->classname);
        }
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
      return m_tvm_compiler->jit_compile(global);
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
     * \brief Create an index term from an integer.
     */
    TreePtr<Term> int_to_index(unsigned index, CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<Term> ty(new PrimitiveType(compile_context, "core.uint.i32", location));
      return TreePtr<IntegerValue>(new IntegerValue(ty, index, location));
    }
    
    /**
     * \brief Convert a constant index to an integer.
     * 
     * \param location Location for error reporting.
     */
    unsigned index_to_int(const TreePtr<Term>& index, const SourceLocation& location) {
      TreePtr<IntegerValue> iv = dyn_treeptr_cast<IntegerValue>(index);
      if (!iv)
        index.compile_context().error_throw(location, "Index into aggregate type is not an IntegerValue term");
      return iv->value;
    }
  }
}
