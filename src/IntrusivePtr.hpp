#ifndef HPP_INTRUSIVE_PTR
#define HPP_INTRUSIVE_PTR

#include <algorithm>

namespace Psi {
  template<typename T>
  struct TypedDeleter {
    void operator () (T* ptr) const {
      delete ptr;
    }
  };

  template<typename T, typename D=TypedDeleter<T> >
  class UnsafeIntrusiveBase : private D {
  public:
    UnsafeIntrusiveBase() {}
    UnsafeIntrusiveBase(D d) : D(std::move(d)) {}

    friend void intrusive_ptr_add_ref(UnsafeIntrusiveBase *ptr) {
      ptr->m_refcount++;
    }

    friend void intrusive_ptr_release(UnsafeIntrusiveBase *ptr) {
      if (--ptr->m_refcount == 0)
        D::operator () (static_cast<T*>(ptr));
    }

  private:
    std::size_t m_refcount;
  };

  template<typename T>
  class IntrusivePtr : public std::iterator_traits<T*> {
  public:
    IntrusivePtr() : m_ptr(0) {}

    IntrusivePtr(IntrusivePtr&& src) {
      m_ptr = src.m_ptr;
      src.m_ptr = 0;
    }

    template<typename U>
    IntrusivePtr(IntrusivePtr<U>&& src) {
      m_ptr = src.m_ptr;
      src.m_ptr = 0;
    }

    IntrusivePtr(const IntrusivePtr& src) : m_ptr(src.m_ptr) {
      if (m_ptr)
        intrusive_ptr_add_ref(m_ptr);
    }

    template<typename U>
    IntrusivePtr(const IntrusivePtr<U>& src) : m_ptr(src.m_ptr) {
      if (m_ptr)
        intrusive_ptr_add_ref(m_ptr);
    }

    IntrusivePtr(T *ptr, bool add_ref=true) : m_ptr(ptr) {
      if (add_ref && m_ptr)
        intrusive_ptr_add_ref(m_ptr);
    }

    ~IntrusivePtr() {
      if (m_ptr)
        intrusive_ptr_release(m_ptr);
    }

    IntrusivePtr& operator = (const IntrusivePtr& src) {
      reset(src.m_ptr);
      return *this;
    }

    template<typename U>
    IntrusivePtr& operator = (const IntrusivePtr<U>& src) {
      reset(src.m_ptr);
      return *this;
    }

    IntrusivePtr& operator = (const T *src) {
      reset(src);
      return *this;
    }

    IntrusivePtr& operator = (IntrusivePtr&& src) {
      swap(IntrusivePtr(std::move(src)));
      return *this;
    }

    template<typename U>
    IntrusivePtr& operator = (IntrusivePtr<U>&& src) {
      swap(IntrusivePtr(std::move(src)));
      return *this;
    }

    void reset(T *src) {
      IntrusivePtr p(src);
      swap(p);
    }

    void reset() {
      IntrusivePtr p;
      swap(p);
    }

    void swap(IntrusivePtr& src) {
      std::swap(m_ptr, src.m_ptr);
    }

    T* get() const {return m_ptr;}
    T& operator * () const {assert(m_ptr); return *m_ptr;}
    T* operator -> () const {assert(m_ptr); return m_ptr;}
    T* release() {T *ptr = m_ptr; m_ptr = 0; return ptr;}

    explicit operator bool () const {return m_ptr;}

  private:
    T *m_ptr;
  };
}

#endif
