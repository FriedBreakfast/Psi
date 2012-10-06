#ifndef HPP_PSI_COMPILER_OBJECTBASE
#define HPP_PSI_COMPILER_OBJECTBASE

#include <boost/array.hpp>
#include <boost/intrusive/list.hpp>

#include "Assert.hpp"
#include "Visitor.hpp"
#include "Runtime.hpp"

namespace Psi {
  namespace Compiler {
    class Object;
    class CompileContext;
    template<typename> class TreePtr;

    /**
     * \brief Single inheritance dispatch table base.
     */
    struct SIVtable {
      const SIVtable *super;
      const char *classname;
      bool abstract;
    };
    
#define PSI_COMPILER_SI(classname,super) {reinterpret_cast<const SIVtable*>(super),classname,false}
#define PSI_COMPILER_SI_ABSTRACT(classname,super) {reinterpret_cast<const SIVtable*>(super),classname,true}
#define PSI_COMPILER_SI_INIT(vptr) (m_vptr = reinterpret_cast<const SIVtable*>(vptr), PSI_ASSERT(!m_vptr->abstract))

    class SIBase;
    
    /**
     * Used to store pointers to tree types in objects, in order to work with the
     * visitor system.
     */
    class SIType {
      const SIVtable *m_vptr;
      
    public:
      SIType() : m_vptr(NULL) {}
      SIType(const SIVtable *vptr) : m_vptr(vptr) {}
    
      const SIVtable* get() const {return m_vptr;}
      operator const SIVtable* () const {return get();}
      const SIVtable* operator -> () const {return get();}
      
      bool isa(const SIBase *obj) const;
      
      template<typename Visitor>
      static void visit(Visitor&) {}
    };

    /**
     * \brief Single inheritance base.
     */
    class SIBase {
      friend bool si_is_a(const SIBase*, const SIVtable*);
      friend const SIVtable* si_vptr(const SIBase*);

    protected:
      const SIVtable *m_vptr;
    };

    inline const SIVtable* si_vptr(const SIBase *self) {return self->m_vptr;}
    bool si_is_a(const SIBase*, const SIVtable*);
    bool si_derived(const SIVtable *base, const SIVtable *derived);

    inline bool SIType::isa(const SIBase *obj) const {return si_is_a(obj, m_vptr);}

    template<typename T>
    const typename T::VtableType* derived_vptr(T *ptr) {
      return reinterpret_cast<const typename T::VtableType*>(si_vptr(ptr));
    }

    template<typename T>
    class ObjectPtr {
      typedef void (ObjectPtr::*safe_bool_type) () const;
      void safe_bool_true() const {}

      T *m_ptr;

      void initialize(T *ptr, bool add_ref) {
        m_ptr = ptr;
        if (add_ref && m_ptr)
          ++m_ptr->m_reference_count;
      }

    public:
      ~ObjectPtr();
      ObjectPtr() : m_ptr(NULL) {}
      explicit ObjectPtr(T *ptr, bool add_ref) {initialize(ptr, add_ref);}
      ObjectPtr(const ObjectPtr& src) {initialize(src.m_ptr, true);}
      template<typename U> ObjectPtr(const ObjectPtr<U>& src) {initialize(src.get(), true);}
      ObjectPtr& operator = (const ObjectPtr& src) {ObjectPtr(src).swap(*this); return *this;}
      template<typename U> ObjectPtr& operator = (const ObjectPtr<U>& src) {ObjectPtr<T>(src).swap(*this); return *this;}

      T* get() const {return m_ptr;}
      T* release() {T *tmp = m_ptr; m_ptr = NULL; return tmp;}
      void swap(ObjectPtr& other) {std::swap(m_ptr, other.m_ptr);}
      void reset(T *ptr=NULL, bool add_ref=true) {ObjectPtr<T>(ptr, add_ref).swap(*this);}

      T& operator * () const {return *get();}
      T* operator -> () const {return get();}

      operator safe_bool_type () const {return get() ? &ObjectPtr::safe_bool_true : 0;}
      bool operator ! () const {return !get();}
      template<typename U> bool operator == (const ObjectPtr<U>& other) const {return get() == other.get();};
      template<typename U> bool operator != (const ObjectPtr<U>& other) const {return get() != other.get();};
      template<typename U> bool operator < (const ObjectPtr<U>& other) const {return get() < other.get();};

      /// \brief Get the compile context for this Object.
      CompileContext& compile_context() const {
        PSI_ASSERT(m_ptr);
        return m_ptr->compile_context();
      }
    };
    
    /// \see Object
    struct ObjectVtable {
      SIVtable base;
      void (*destroy) (Object*);
      void (*gc_increment) (Object*);
      void (*gc_decrement) (Object*);
      void (*gc_clear) (Object*);
    };

    /**
     * Extends SIBase to participate in garbage collection.
     */
    class Object : public SIBase, public boost::intrusive::list_base_hook<> {
      friend class CompileContext;
      friend class GCVisitorIncrement;
      friend class GCVisitorDecrement;
      template<typename> friend class ObjectPtr;

      mutable std::size_t m_reference_count;
      CompileContext *m_compile_context;

    public:
      typedef ObjectVtable VtableType;
      static const SIVtable vtable;

      Object(const ObjectVtable *vtable, CompileContext& compile_context);
      ~Object();

      CompileContext& compile_context() const {return *m_compile_context;}
      
      template<typename V> static void visit(V&) {}
    };
    
    template<typename T>
    ObjectPtr<T>::~ObjectPtr() {
      if (m_ptr) {
        if (!--m_ptr->m_reference_count) {
          const Object *cptr = m_ptr;
          Object *optr = const_cast<Object*>(cptr);
          derived_vptr(optr)->destroy(optr);
        }
      }
    }

    /**
     * \brief Base classes for gargabe collection phase
     * implementations.
     */
    template<typename Derived>
    class ObjectVisitorBase {
      Derived& derived() {
        return *static_cast<Derived*>(this);
      }

    public:
      template<typename T>
      void visit_base(const boost::array<T*,1>& c) {
        if (derived().do_visit_base(VisitorTag<T>()))
          visit_members(derived(), c);
      }
      
      template<typename T>
      bool do_visit_base(VisitorTag<T>) {
        return true;
      }

      /// Simple types cannot hold references, so we aren't interested in them.
      template<typename T>
      void visit_simple(const char*, const boost::array<T*, 1>&) {
      }

      template<typename T>
      void visit_object(const char*, const boost::array<T*,1>& obj) {
        visit_members(*this, obj);
      }

      /// Simple pointers are assumed to be owned by this object
      template<typename T>
      void visit_object(const char*, const boost::array<T**,1>& obj) {
        if (*obj[0]) {
          boost::array<T*, 1> star = {{*obj[0]}};
          visit_callback(*this, NULL, star);
        }
      }

      /// Shared pointers cannot reference trees (this would break the GC), so they are ignored.
      template<typename T>
      void visit_object(const char*, const boost::array<SharedPtr<T>*,1>&) {
      }

      template<typename T>
      void visit_object(const char*, const boost::array<ObjectPtr<T>*,1>& ptr) {
        derived().visit_object_ptr(*ptr[0]);
      }

      template<typename T>
      void visit_object(const char*, const boost::array<TreePtr<T>*, 1>& ptr) {
        derived().visit_tree_ptr(*ptr[0]);
      }

      template<typename T>
      void visit_sequence (const char*, const boost::array<T*,1>& collections) {
        for (typename T::iterator ii = collections[0]->begin(), ie = collections[0]->end(); ii != ie; ++ii) {
          boost::array<typename T::value_type*, 1> m = {{&*ii}};
          visit_callback(*this, NULL, m);
        }
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*,1>& maps) {
        for (typename T::iterator ii = maps[0]->begin(), ie = maps[0]->end(); ii != ie; ++ii) {
#if 0
          boost::array<const typename T::key_type*, 1> k = {{&ii->first}};
          visit_object(NULL, k);
#endif
          boost::array<typename T::mapped_type*, 1> v = {{&ii->second}};
          visit_callback(*this, NULL, v);
        }
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorIncrement : public ObjectVisitorBase<GCVisitorIncrement> {
    public:
      template<typename T>
      void visit_object_ptr(const ObjectPtr<T>& ptr) {
        if (ptr)
          ++ptr->m_reference_count;
      }
      
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        visit_object_ptr(ptr.raw_ptr_get());
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorDecrement : public ObjectVisitorBase<GCVisitorDecrement> {
    public:
      template<typename T>
      void visit_object_ptr(const ObjectPtr<T>& ptr) {
        if (ptr)
          --ptr->m_reference_count;
      }
      
      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        visit_object_ptr(ptr.raw_ptr_get());
      }
    };

    /**
     * \brief Implements the increment phase of the garbage collector.
     */
    class GCVisitorClear : public ObjectVisitorBase<GCVisitorClear> {
    public:
      template<typename T>
      void visit_sequence(const char*, const boost::array<T*,1>& seq) {
        seq[0]->clear();
      }

      template<typename T>
      void visit_map(const char*, const boost::array<T*,1>& maps) {
        maps[0]->clear();
      }

      template<typename T>
      void visit_object_ptr(ObjectPtr<T>& ptr) {
        ptr.reset();
      }

      template<typename T>
      void visit_tree_ptr(TreePtr<T>& ptr) {
        ptr.reset();
      }
    };

    template<typename Derived>
    struct ObjectWrapper : NonConstructible {
      static void destroy(Object *self) {
        delete static_cast<Derived*>(self);
      }

      static void gc_increment(Object *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorIncrement p;
        visit_members(p, a);
      }

      static void gc_decrement(Object *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorDecrement p;
        visit_members(p, a);
      }

      static void gc_clear(Object *self) {
        boost::array<Derived*, 1> a = {{static_cast<Derived*>(self)}};
        GCVisitorClear p;
        visit_members(p, a);
      }
    };

#define PSI_COMPILER_OBJECT(derived,name,super) { \
    PSI_COMPILER_SI(name,&super::vtable), \
    &::Psi::Compiler::ObjectWrapper<derived>::destroy, \
    &::Psi::Compiler::ObjectWrapper<derived>::gc_increment, \
    &::Psi::Compiler::ObjectWrapper<derived>::gc_decrement, \
    &::Psi::Compiler::ObjectWrapper<derived>::gc_clear \
  }
  }
}

#endif
