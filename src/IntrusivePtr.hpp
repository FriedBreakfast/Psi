#ifndef HPP_INTRUSIVE_PTR
#define HPP_INTRUSIVE_PTR

#include <algorithm>

#include <boost/intrusive_ptr.hpp>

namespace Psi {
  namespace IntrusivePtr {
    template<typename T>
    struct TypedDeleter {
      void operator () (T* ptr) const {
	delete ptr;
      }
    };

    template<typename T, typename D=TypedDeleter<T> >
    class IntrusiveBase : private D {
    public:
      IntrusiveBase() {}
      IntrusiveBase(D d) : D(std::move(d)) {}

      friend void intrusive_ptr_add_ref(T *ptr) {
	ptr->m_refcount++;
      }

      friend void intrusive_ptr_release(T *ptr) {
	if (--ptr->m_refcount == 0)
	  D::operator () (ptr);
      }

    private:
      std::size_t m_refcount;
    };
  }
}

#endif
