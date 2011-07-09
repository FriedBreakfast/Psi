#ifndef HPP_PSI_RUNTIME
#define HPP_PSI_RUNTIME

#include <utility>
#include <algorithm>
#include <iosfwd>
#include <boost/aligned_storage.hpp>
#include <boost/static_assert.hpp>

#include <vector>
#include <map>

#include "Assert.hpp"
#include "CppCompiler.hpp"
#include "Utility.hpp"

/**
 * \file
 * 
 * This file contains the C++ interface to various Psi types. Most of these
 * mirror existing C++ types but I need a portable binary interface so using
 * the C++ types is out of the question.
 */

namespace Psi {
  void* checked_alloc(std::size_t n);
  void checked_free(std::size_t n, void *ptr);

  template<std::size_t N>
  class StackBuffer : boost::noncopyable {
    char m_buffer[N] PSI_ATTRIBUTE((PSI_ALIGNED_MAX));
    void *m_ptr;
    std::size_t m_size;

  public:
    StackBuffer(std::size_t m) : m_size(m) {
      if (m <= N)
        m_ptr = &m_buffer;
      else
        m_ptr = checked_alloc(m);
    }

    ~StackBuffer() {
      if (m_size > N)
        checked_free(m_size, m_ptr);
    }

    void* get() {return m_ptr;}
  };

  typedef char PsiBool;
  typedef std::size_t PsiSize;

  enum PsiBoolValues {
    psi_false = 0,
    psi_true = 1
  };

  struct SharedPtrOwner;
  
  struct SharedPtrOwnerVTable {
    void (*destroy) (SharedPtrOwner*);
  };
  
  struct SharedPtrOwner {
    SharedPtrOwnerVTable *vptr;
    PsiSize use_count;
  };
  
  /**
   * \brief C layout of SharedPtr.
   */
  struct SharedPtr_C {
    void *ptr;
    SharedPtrOwner *owner;
  };

  template<typename U>
  struct SharedPtrOwnerImpl : SharedPtrOwner {
    SharedPtrOwnerVTable vtable;
    U *ptr;
  };

  template<typename U>
  void shared_ptr_delete(SharedPtrOwner *owner) {
    SharedPtrOwnerImpl<U> *self = static_cast<SharedPtrOwnerImpl<U>*>(owner);
    delete self->ptr;
    delete self;
  }

  /**
   * \brief Shared pointer class.
   *
   * This wraper SharedPtr_C, and is designed to be interface-compatible
   * with boost::shared_ptr, excluding custom allocator stuff.
   */
  template<typename T>
  class SharedPtr {
    SharedPtr_C m_c;

    void init(T *ptr, SharedPtrOwner *owner) {
      m_c.ptr = ptr;
      m_c.owner = owner;
      if (owner)
        ++owner->use_count;
    }

    typedef void (SharedPtr::*safe_bool_type) () const;
    void safe_bool_true() const {}

  public:
    template<typename> friend class SharedPtr;
    typedef T value_type;

    SharedPtr() {
      m_c.ptr = 0;
      m_c.owner = 0;
    }

    template<typename U>
    SharedPtr(U *ptr) {
      SharedPtrOwnerImpl<U> *owner;
      try {
        owner = new SharedPtrOwnerImpl<U>();
      } catch (...) {
        delete ptr;
        throw;
      }
      owner->vptr = &owner->vtable;
      owner->use_count = 1;
      owner->vtable.destroy = &shared_ptr_delete<U>;
      owner->ptr = ptr;

      m_c.owner = owner;
      m_c.ptr = ptr;
    }

    SharedPtr(const SharedPtr& src) {
      init(src.get(), src.m_c.owner);
    }

    template<typename U>
    SharedPtr(const SharedPtr<U>& src) {
      init(src.get(), src.m_c.owner);
    }

    template<typename U>
    SharedPtr(const SharedPtr<U>& src, T *ptr) {
      init(ptr, src.m_c.owner);
    }

    ~SharedPtr() {
      if (m_c.owner) {
        if (--m_c.owner->use_count == 0)
          m_c.owner->vptr->destroy(m_c.owner);
      }
    }
    
    T* get () const {return static_cast<T*>(m_c.ptr);}
    T& operator * () const {return *get();}
    T* operator -> () const {return get();}

    SharedPtr& operator = (const SharedPtr& src) {
      SharedPtr<T>(src).swap(*this);
      return *this;
    }
    
    template<typename U>
    SharedPtr& operator = (const SharedPtr<U>& src) {
      SharedPtr<T>(src).swap(*this);
      return *this;
    }

    void swap(SharedPtr& other) {
      std::swap(m_c, other.m_c);
    }

    template<typename U>
    void reset(U *ptr) {
      SharedPtr<T>(ptr).swap(*this);
    }

    void reset() {
      SharedPtr<T>().swap(*this);
    }

    bool operator ! () const {return !get();}
    operator safe_bool_type () const {return get() ? &SharedPtr::safe_bool_true : 0;}
  };

  template<typename T, typename U>
  bool operator == (const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) {
    return lhs.get() == rhs.get();
  }

  template<typename T, typename U>
  bool operator != (const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) {
    return lhs.get() != rhs.get();
  }

  template<typename T, typename U>
  bool operator < (const SharedPtr<T>& lhs, const SharedPtr<U>& rhs) {
    return lhs.get() < rhs.get();
  }

  template<typename T, typename U>
  SharedPtr<T> checked_pointer_cast(const SharedPtr<U>& src) {
    PSI_ASSERT(dynamic_cast<T*>(src.get()) == src.get());
    return SharedPtr<T>(src, static_cast<T*>(src.get()));
  }

  struct String_C {
    PsiSize length;
    char *data;
  };
  
  class String {
    String_C m_c;

    void init(const char*, PsiSize);
    
  public:
    String();
    String(const String&);
    String(const char*);
    String(const char*, const char*);
    String(const char*, PsiSize);
    ~String();
    
    String& operator = (const String&);
    String& operator = (const char*);

    bool operator == (const String&) const;
    bool operator != (const String&) const;
    bool operator < (const String&) const;

    void clear();
    const char *c_str() const {return m_c.data;}
    bool empty() const {return !m_c.length;}
    void swap(String&);
  };

  std::ostream& operator << (std::ostream&, const String&);
  
  template<typename T>
  struct Maybe_C {
    PsiBool full;
    AlignedStorageFor<T> data;
  };
  
  template<typename T>
  class Maybe {
    Maybe_C<T> m_c;
    
    T* unchecked_get() {return m_c.data.ptr();}
    const T* unchecked_get() const {return m_c.data.ptr();}
    
  public:
    Maybe() {
      m_c.full = psi_false;
    }
    
    Maybe(const T& value) {
      m_c.full = psi_true;
      new (m_c.data) T (value);
    }
    
    Maybe(const Maybe<T>& other) {
      if (other) {
        m_c.full = psi_true;
        new (m_c.data) T (*other);
      } else {
        m_c.full = psi_false;
      }
    }
    
    ~Maybe() {
      if (m_c.full)
        get()->~T();
    }
    
    bool empty() const {return !m_c.full;}
    T* get() {return m_c.full ? unchecked_get() : 0;}
    const T* get() const {return m_c.full ? unchecked_get() : 0;}
    
    T* operator -> () {PSI_ASSERT(!empty()); return unchecked_get();}
    const T* operator -> () const {PSI_ASSERT(!empty()); return unchecked_get();}
    T& operator * () {PSI_ASSERT(!empty()); return *unchecked_get();}
    const T& operator * () const {PSI_ASSERT(!empty()); return *unchecked_get();}

    void clear() {
      if (m_c.full) {
        delete unchecked_get()->~T();
        m_c.full = psi_false;
      }
    }
    
    Maybe& operator = (const Maybe<T>& other) {
      if (other)
        operator = (*other);
    }
    
    Maybe& operator = (const T& src) {
      if (m_c.m_full)
        *unchecked_get() = src;
      else
        new (m_c.data) T (src);
    }
  };

  enum LookupResultType {
    lookup_result_type_match, ///< \brief Match found
    lookup_result_type_none, ///< \brief No match found
    lookup_result_type_conflict ///< \brief Multiple ambiguous matches found
  };

  template<typename T>
  struct LookupResult_C {
    LookupResultType type;
    AlignedStorageFor<T> data;
  };

  struct LookupResultNoneHelper {};
  typedef int LookupResultNoneHelper::*LookupResultNone;
  struct LookupResultConflictHelper {};
  typedef int LookupResultConflictHelper::*LookupResultConflict;

  const LookupResultNone lookup_result_none = 0;
  const LookupResultConflict lookup_result_conflict = 0;

  template<typename T>
  class LookupResult {
    LookupResult_C<T> m_c;

  public:
    LookupResult(LookupResultNone) {
      m_c.type = lookup_result_type_none;
    }
    
    LookupResult(LookupResultConflict) {
      m_c.type = lookup_result_type_conflict;
    }
    
    LookupResult(const T& value) {
      m_c.type = lookup_result_type_match;
      new (m_c.data.ptr()) T(value);
    }
    
    template<typename U>
    LookupResult(const LookupResult<U>& src) {
      m_c.type = src.type();
      if (m_c.type == lookup_result_type_match)
        new (m_c.data.ptr()) T(src.value());
    }

    ~LookupResult() {
      if (m_c.type == lookup_result_type_match)
        delete m_c.data.ptr();
    }

    LookupResultType type() const {
      return m_c.type;
    }

    const T& value() const {
      PSI_ASSERT(type() == lookup_result_type_match);
      return *m_c.data.ptr();
    }
  };

  template<typename T>
  LookupResult<T> lookup_result_match(const T& value) {
    return LookupResult<T>(value);
  }

  class InterfaceBase {
  protected:
    const void *m_vptr;
    void *m_self;

  public:
    InterfaceBase(const void *vptr, void *self) : m_vptr(vptr), m_self(self) {}
    const void *vptr() const {return m_vptr;}
    void *object() const {return m_self;}
  };

  struct BaseVtable {
    PsiSize size;
    PsiSize alignment;
    void (*move) (const void*, void*, void*);
    void (*destroy) (const void*, void*);
  };

  template<typename Impl>
  struct BaseWrapper : NonConstructible {
    typedef typename Impl::ObjectType ObjectType;

    static void move(const void*, void *dest, void *src) {
      Impl::move_impl(*static_cast<ObjectType*>(dest), *static_cast<ObjectType*>(src));
    }

    static void destroy(const void*, void* self) {
      Impl::destroy_impl(*static_cast<ObjectType*>(self));
    }
  };

#define PSI_BASE(impl) { \
  sizeof(typename impl::ObjectType), \
  PSI_ALIGNOF(typename impl::ObjectType), \
  &BaseWrapper<impl>::move, \
  &BaseWrapper<impl>::destroy \
}

  struct IteratorObject;

  struct IteratorVtable {
    BaseVtable super;
    void* (*current) (const void*, void*);
    PsiBool (*next) (const void*, void*);
  };

  template<typename T>
  class Iterator : public InterfaceBase {
  public:
    Iterator(const void *vptr, void *self) : InterfaceBase(vptr, self) {}
    const IteratorVtable *vptr() const {return static_cast<const IteratorVtable*>(this->m_vptr);}
    bool next() const {return vptr()->next(vptr(), this->m_self);}
    T& current() const {return *static_cast<T*>(vptr()->current(vptr(), this->m_self));}
  };

  template<typename Impl>
  struct IteratorWrapper : NonConstructible {
    typedef typename Impl::ObjectType ObjectType;

    static void* current(const void*, void *self) {
      return &Impl::current_impl(*static_cast<ObjectType*>(self));
    }

    static PsiBool next(const void*, void *self) {
      return Impl::next_impl(*static_cast<ObjectType*>(self));
    }
  };

#define PSI_ITERATOR(impl) { \
  PSI_BASE(impl), \
  &IteratorWrapper<impl>::current, \
  &IteratorWrapper<impl>::next \
  }

  struct ListObject;

  /**
   * \brief vtable for list types.
   */
  struct ListVtable {
    IteratorVtable iterator_vtable;
    void (*iterator) (const void*, void*, void*);
    PsiSize (*size) (const void*, void*);
    void* (*get) (const void*, void*, PsiSize);
  };

  template<typename T>
  class List : public InterfaceBase {
  public:
    typedef T IteratorValueType;

    List(const void *vptr, void *self) : InterfaceBase(vptr, self) {}

    const ListVtable *vptr() const {return static_cast<const ListVtable*>(this->m_vptr);}

    PsiSize size() const {return vptr()->size(vptr(), this->m_self);}

    T& operator [] (PsiSize n) const {return *static_cast<T*>(vptr()->get(vptr(), this->m_self, n));}

    friend const IteratorVtable* iterator_vptr(const List& self) {
      return &self.vptr()->iterator_vtable;
    }

    friend void iterator_init(void *dest, const List& self) {
      self.vptr()->iterator(self.vptr(), dest, self.object());
    }
  };

  template<typename Impl>
  struct ListWrapper : NonConstructible {
    typedef typename Impl::ObjectType ObjectType;

    static void iterator(const void*, void *dest, void *self) {
      Impl::iterator_impl(dest, *static_cast<ObjectType*>(self));
    }

    static PsiSize size(const void*, void *self) {
      return Impl::size_impl(*static_cast<ObjectType*>(self));
    }

    static void* get(const void*, void *self, PsiSize index) {
      return &Impl::get_impl(*static_cast<ObjectType*>(self), index);
    }
  };

#define PSI_LIST(impl) { \
  PSI_ITERATOR(impl::IteratorImpl), \
  &ListWrapper<impl>::iterator, \
  &ListWrapper<impl>::size, \
  &ListWrapper<impl>::get \
}
  
  template<typename T>
  struct ContainerListFunctions : NonConstructible {
    typedef T ContainerType;
    typedef typename ContainerType::value_type ValueType;
    typedef ContainerType ObjectType;
    
    static const ListVtable vtable;

    struct Iterator {
      typename ContainerType::iterator current, end;
      Iterator(ContainerType& container) : current(container.begin()), end(container.end()) {}
    };

    struct IteratorImpl {
      typedef Iterator ObjectType;

      static void move_impl(Iterator& dest, Iterator& src) {
        std::swap(dest, src);
      }

      static void destroy_impl(Iterator& self) {
        self.~Iterator();
      }

      static ValueType& current_impl(Iterator& self) {
        return *self.current;
      }

      static bool next_impl(Iterator& self) {
        return ++self.current != self.end;
      }
    };

    static void iterator_impl(void *dest, ContainerType& self) {
      new (dest) Iterator(self);
    }

    static PsiSize size_impl(ContainerType& self) {
      return self.size();
    }

    static ValueType& get_impl(ContainerType& self, PsiSize n) {
      return self[n];
    }
  };

  template<typename T> const ListVtable ContainerListFunctions<T>::vtable = PSI_LIST(ContainerListFunctions<T>);

  template<typename T>
  List<typename T::value_type> list_from_stl(T& list) {
    return List<typename T::value_type>(&ContainerListFunctions<T>::vtable, &list);
  }

  template<typename T>
  class LocalIterator : public Iterator<T> {
    StackBuffer<32> m_data;

  public:
    template<typename U>
    LocalIterator(const U& list)
    : Iterator<T>(iterator_vptr(list), 0), m_data(iterator_vptr(list)->super.size) {
      BOOST_STATIC_ASSERT((boost::is_same<T, typename U::IteratorValueType>::value));
      this->m_self = m_data.get();
      iterator_init(m_data.get(), list);
    }

    template<typename U>
    LocalIterator(U& list)
    : Iterator<T>(iterator_vptr(list), 0), m_data(iterator_vptr(list)->super.size) {
      BOOST_STATIC_ASSERT((boost::is_same<T, typename U::IteratorValueType>::value));
      this->m_self = m_data.get();
      iterator_init(m_data.get(), list);
    }

    ~LocalIterator() {
      this->vptr()->super.destroy(this->vptr(), this->object());
    }
  };

  struct MapObject;

  /**
   * \brief vtable for map types.
   */
  struct MapVtable {
    void* (*get) (const void*,void*,const void*);
  };

  template<typename T>
  struct ContainerMapFunctions {
    typedef T ContainerType;
    typedef typename T::key_type KeyType;
    typedef typename T::mapped_type DataType;
    
    static const MapVtable vtable;

    static void* get(const void*, void *self, const void *key) {
      ContainerType *cast_self = static_cast<ContainerType*>(self);
      const KeyType *cast_key = static_cast<const KeyType*>(key);
      typename ContainerType::iterator it = cast_self->find(*cast_key);
      return it != cast_self->end() ? &it->second : NULL;
    }
  };

  template<typename T> const MapVtable ContainerMapFunctions<T>::vtable = {
    &ContainerMapFunctions<T>::get
  };

  template<typename T, typename Key, typename Value>
  struct MapWrapper : NonConstructible {
    static void* get(const void*, void *self, const void *key) {
      return T::get_impl(*static_cast<T*>(self), *static_cast<const Key*>(key));
    }
  };

#define PSI_MAP(Ty,Key,Value) { \
    &MapWrapper<Ty,Key,Value>::get \
  }

  template<typename Key, typename Value>
  class Map : public InterfaceBase {
  public:
    Map(const void *vptr, void *self) : InterfaceBase(vptr, self) {}
    Map(std::map<Key, Value>& src) : InterfaceBase(vptr_for(src), object_for(src)) {}

    static const void* vptr_for(std::map<Key, Value>&) {return &ContainerMapFunctions<std::map<Key, Value> >::vtable;}
    static void* object_for(std::map<Key, Value>& obj) {return &obj;}
    
    const MapVtable *vptr() const {return static_cast<const MapVtable*>(this->m_vptr);}

    Value* get(const Key& key) const {return static_cast<Value*>(vptr()->get(vptr(), this->m_self, &key));}
  };
}

#endif
