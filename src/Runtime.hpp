#ifndef HPP_PSI_RUNTIME
#define HPP_PSI_RUNTIME

#include <utility>
#include <algorithm>

#include "Assert.hpp"
#include "CppCompiler.hpp"

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
  
  template<typename T>
  struct MoveRef {
    T *x;
    MoveRef(T& x_) : x(&x_) {}
    T* operator -> () {return x;}
    operator const T& () {return *x;}
  };

  template<typename T>
  MoveRef<T> move_ref(T& x) {
    return MoveRef<T>(x);
  }
  
  struct SharedPtrOwner;
  
  struct SharedPtrOwnerVTable {
    void (*destroy) (SharedPtrOwner*);
  };
  
  struct SharedPtrOwner {
    SharedPtrOwnerVTable *vptr;
    std::size_t use_count;
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
    SharedPtrOwnerImpl<U> *self = static_cast<SharedPtrOwnerImpl<U> >(owner);
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

  public:
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
    SharedPtr(MoveRef<SharedPtr<U> > src) {
      T *ptr = src->get();
      m_c.ptr = ptr;
      m_c.owner = src->m_c.owner;
      src->m_c.ptr = 0;
      src->m_c.owner = 0;
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

    template<typename U>
    SharedPtr& operator = (MoveRef<SharedPtr<U> > src) {
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

  struct String_C {
    std::size_t length;
    char *data;
  };
  
  class String {
    String_C m_c;
    
  public:
    String();
    String(const String&);
    String(MoveRef<String>);
    String(const char*);
    String(const char*, std::size_t);
    ~String();
    
    String& operator = (const String&);
    String& operator = (MoveRef<String>);
    String& operator = (const char*);

    bool operator == (const String&) const;
    bool operator != (const String&) const;
    bool operator < (const String&) const;
    
    void clear();
    const char *c_str() const {return m_c.data;}
  };
  
  template<typename T>
  struct Maybe_C {
    bool full;
    char data[sizeof(T)] PSI_ATTRIBUTE((PSI_ALIGNED(PSI_ALIGNOF(T))));
  };
  
  template<typename T>
  class Maybe {
    Maybe_C<T> m_c;
    
    T* unchecked_get() {return static_cast<T*>(m_c.data);}
    const T* unchecked_get() const {return static_cast<const T*>(m_c.data);}
    
  public:
    Maybe() {
      m_c.full = false;
    }
    
    Maybe(const T& value) {
      m_c.full = true;
      new (m_c.data) T (value);
    }
    
    Maybe(MoveRef<T> value) {
      m_c.full = true;
      new (m_c.data) T (value);
    }
    
    
    Maybe(const Maybe<T>& other) {
      if (other) {
        m_c.full = true;
        new (m_c.data) T (*other);
      } else {
        m_c.full = false;
      }
    }
    
    Maybe(MoveRef<Maybe<T> > other) {
      if (*other) {
        m_c.full = true;
        new (m_c.data) T (MoveRef<T>(**other));
        other->clear();
      } else {
        m_c.full = false;
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
        m_c.full = false;
      }
    }
    
    Maybe& operator = (const Maybe<T>& other) {
      if (other)
        operator = (*other);
    }
    
    Maybe& operator = (MoveRef<Maybe<T> > other) {
      if (other) {
        operator = (MoveRef<T>(**other));
        other->clear();
      }
    }

    Maybe& operator = (const T& src) {
      if (m_c.m_full)
        *unchecked_get() = src;
      else
        new (m_c.data) T (src);
    }
    
    Maybe& operator = (MoveRef<T> src) {
      if (m_c.full)
        *unchecked_get() = src;
      else
        new (m_c.data) T (src);
    }
  };

  /**
   * \brief Array list C layout.
   */
  struct ArrayList_C {
    void *begin;
    void *end;
    void *limit;
  };

  /**
   * \brief Array list.
   *
   * This is not expected to cover all features that are supported by Psi's
   * ArrayList type. This can be used to create ArrayLists in C++, but should
   * not be used to modify existing array lists since the memory allocation
   * strategy used here is fixed.
   */
  template<typename T>
  class ArrayList {
    ArrayList_C m_c;

    void init() {
      m_c.begin = 0;
      m_c.end = 0;
      m_c.limit = 0;
    }
    
  public:
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T* iterator;
    typedef const T* const_iterator;

    ArrayList() {
      init();
    }

    ArrayList(size_type n) {
      init();
      resize(n);
    }

    ~ArrayList() {
      if (m_c.begin) {
        clear();
        std::free(m_c.begin);
      }
    }
    
    ArrayList(const ArrayList<T>& src) {
      init();
      reserve(src.size());
      iterator j = begin();
      for (iterator ii = src.begin(), ie = src.end(); ii != ie; ++ii)
        new (*j++) T (*ii);
      m_c.end = j;
    }

    ArrayList(MoveRef<ArrayList<T> > src) {
      m_c = src.m_c;
      src.init();
    }
    
    template<typename It>
    ArrayList(const It& first, const It& last) {
      init();
      insert(end(), first, last);
    }
    
    ArrayList(size_type n, const T& x=T()) {
      init();
      reserve(n);
      for (size_type i = 0; i != n; ++i)
        push_back(x);
    }

  private:
    struct CapacityLabel {};
    ArrayList(CapacityLabel, size_type capacity) {
      m_c.begin = m_c.end = checked_alloc(capacity * sizeof(T));
      m_c.limit = static_cast<T*>(m_c.begin) + capacity;
    }

  public:
    iterator begin() {return static_cast<T*>(m_c.begin);}
    iterator end() {return static_cast<T*>(m_c.end);}
    const_iterator begin() const {return static_cast<const T*>(m_c.begin);}
    const_iterator end() const {return static_cast<const T*>(m_c.end);}
    size_type size() const {return end() - begin();}
    size_type capacity() const {return static_cast<const T*>(m_c.limit) - begin();}

    /**
     * \brief Ensure enough capacity to add n elements.
     */
    void ensure_capacity(size_type n) {
      reserve(size() + n);
    }

    /**
     * \brief Prepare to insert n elements at position i.
     */
    iterator prepare_insert(iterator i, size_type n) {
      size_type off = i - begin();
      ensure_capacity(n);
      for (T *pi = end() + n, *pe = end(); pi != pe; --pi)
        new (pi - 1) T(move_ref(*(pi - n - 1)));
      for (T *pi = end(), *pe = begin() + off + n; pi != pe; --pi)
        *(pi - 1) = move_ref(*(pi - n - 1));
      return begin() + off;
    }

    iterator insert(iterator i, const_reference x) {
      iterator j = prepare_insert(i, 1);
      *j = x;
      return j;
    }

    iterator insert(iterator i, MoveRef<T> x) {
      iterator j = prepare_insert(i, 1);
      *j = x;
      return j;
    }
    
    template<typename It>
    iterator insert(iterator i, const It& b, const It& e) {
      for (It j = b; j != e; ++j) {
        i = insert(i, *j);
        ++i;
      }
    }

    iterator erase(iterator first, iterator last) {
      size_type delta = last - first;
      for (T *pi = first, *pe = end() - delta; pi != pe; ++pi)
        *pi = move_ref(*(pi + delta));
      for (T *pi = end() - delta, *pe = end(); pi != pe; ++pi)
        pi->~T();
      m_c.end = end() - delta;
      return first;
    }

    iterator erase(iterator i) {
      return erase(i, i+1);
    }

    void reserve(size_type n) {
      if (n > capacity()) {
        size_type new_capacity = std::max(size_type(4), std::max(capacity() * size_type(2), n));
        ArrayList other(CapacityLabel(), new_capacity);
        other.swap(*this);
      }
    }

    void resize(size_type n, const T& x=T()) {
      reserve(n);
      while (size() < n)
        push_back(x);
    }

    void clear() {
      T *p = end();
      do {
        --p;
        p->~T();
      } while (p != m_c.begin);
      m_c.end = m_c.begin;
    }

    void push_back(const_reference x) {insert(end(), x);}
    void push_back(MoveRef<T> x) {insert(end(), x);}

    void swap(ArrayList<T>& other) {
      std::swap(m_c, other.m_c);
    }
    
    T& front() {return *begin();}
    const T& front() const {return *begin();}
  };
}

#endif
