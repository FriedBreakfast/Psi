#ifndef HPP_PSI_LAZY_COPY
#define HPP_PSI_LAZY_COPY

namespace Psi {
  /**
   * \brief Lazily copies an existing class.
   *
   * This requires that the existing object is not destroyed until
   * after the lazy copy is. Copy construction is not allowed but move
   * construction is: in this case the original object must outlive
   * both the source and target of the move.
   */
  template<typename T>
  class LazyCopy {
  public:
    LazyCopy(LazyCopy&& src) {
      if (src.m_original) {
        m_original = src.m_original;
      } else {
        new (storage()) T (std::move(*src.storage()));
      }
    }

    LazyCopy(const T& original) : m_original(&original) {
    }

    const T& operator * () const {return *active();}
    const T* operator -> () const {return active();}

    T& writable() {
      if (m_original) {
        new (storage()) T (*m_original);
        m_original = 0;
      }
      return *storage();
    }

  private:
    const T *m_original;
    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage;

    const T* active() const {return m_original ? m_original : storage();}
    T* storage() {return static_cast<T*>(static_cast<void*>(&m_storage));}
    const T* storage() const {return static_cast<const T*>(static_cast<const void*>(&m_storage));}

    LazyCopy(const LazyCopy&);
  };

  /**
   * Factory function for #LazyCopy.
   */
  template<typename T>
  LazyCopy<T> make_lazy_copy(const T& original) {
    return LazyCopy<T>(original);
  }
}

#endif
