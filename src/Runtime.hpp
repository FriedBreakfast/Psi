#ifndef HPP_PSI_RUNTIME
#define HPP_PSI_RUNTIME

#include <cstdlib>
#include <utility>
#include <algorithm>

/**
 * \file
 * 
 * This file contains the C++ interface to various Psi types. Most of these
 * mirror existing C++ types but I need a portable binary interface so using
 * the C++ types is out of the question.
 */

namespace Psi {
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
    typedef T value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T* iterator;

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

    ArrayList(MoveRef<ArrayList<T> > src) {
      m_c = src.m_c;
      src.init();
    }

    ArrayList(bool, size_type capacity) {
      m_c.begin = m_c.end = std::malloc(capacity * sizeof(T));
      m_c.limit = static_cast<T*>(m_c.begin) + capacity;
    }

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

    void insert(iterator i, const_reference x) {
      iterator j = prepare_insert(i, 1);
      *j = x;
      return j;
    }

    void insert_move(iterator i, MoveRef<T> x) {
      iterator j = prepare_insert(i, 1);
      *j = x;
      return j;
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
        size_type new_capacity = std::max(4, std::max(capacity() * 2, n));
        ArrayList other(false, new_capacity);
        other.swap(*this);
      }
    }

    void resize(size_type n, const T& x=T()) {
      reserve(n);
      while (size() < n)
        push_back(x);
    }

    void clear() {
      T *end = m_c.end;
      do {
        --end;
        end->~T();
      } while (end != m_c.begin);
      m_c.end = m_c.begin;
    }

    void push_back(const_reference x) {insert(end(), x);}
    void push_back(MoveRef<T> x) {insert(end(), x);}

    void swap(ArrayList<T>& other) {
      std::swap(m_c, other.m_c);
    }
  };
}

#endif
