#ifndef HPP_PSI_ARRAY
#define HPP_PSI_ARRAY

#include <boost/noncopyable.hpp>

#include "Assert.hpp"

namespace Psi {
  template<typename> class ArrayPtr;
  
  /**
   * \brief Base class for various array classes.
   * 
   * This is noncopyable so that inheriting classes must supply
   * copy and assignment operators manually.
   */
  template<typename T>
  class ArrayWithLength : public boost::noncopyable {
    typedef void (ArrayWithLength::*SafeBoolType)() const;
    void safe_bool_true() const {}

  protected:
    T *m_ptr;
    std::size_t m_size;
    
    ArrayWithLength() : m_ptr(0), m_size(0) {}
    ArrayWithLength(T *ptr, std::size_t size) : m_ptr(ptr), m_size(size) {}

  public:
    T* get() const {return m_ptr;}
    T& operator [] (std::size_t n) const {PSI_ASSERT(n < m_size); return m_ptr[n];}
    std::size_t size() const {return m_size;}
    ArrayPtr<T> slice(std::size_t, std::size_t);
  };

  /**
   * \brief Array reference which also stores a length.
   *
   * This allows a pointer and a length to be passed around easily,
   * but does not perform any memory management.
   * 
   * This is copy constructible but not assignable since these should
   * be temporary objects.
   */
  template<typename T>
  class ArrayPtr : public ArrayWithLength<T> {
  public:
    ArrayPtr() {}
    ArrayPtr(T *ptr, std::size_t size) : ArrayWithLength<T>(ptr, size) {}
    ArrayPtr(const ArrayPtr& src) : ArrayWithLength<T>(src.get(), src.size()) {}
    template<typename U> ArrayPtr(const ArrayWithLength<U>& src) : ArrayWithLength<T>(src.get(), src.size()) {}
    template<typename U, typename Alloc>
    ArrayPtr(const std::vector<U, Alloc>& src) : ArrayWithLength<T>(&src.front(), src.size()) {}
    
    ArrayPtr& operator = (const ArrayPtr& src) {
      this->m_ptr = src.m_ptr;
      this->m_size = src.m_size;
      return *this;
    }
    
    template<typename U>
    ArrayPtr& operator = (const ArrayWithLength<U>& src) {
      this->m_ptr = src.get();
      this->m_size = src.size();
      return *this;
    }
  };
  
  /**
   * Array slice operation - returns an ArrayPtr which is a view on this
   * array from start to end (end is not included so that end-start elements
   * are available).
   */
  template<typename T>
  ArrayPtr<T> ArrayWithLength<T>::slice(std::size_t start, std::size_t end) {
    PSI_ASSERT((start <= end) && (end <= m_size));
    return ArrayPtr<T>(m_ptr+start, end-start);
  }
  
  /**
   * \brief Scoped, dynamically allocated array.
   * 
   * This allocates the array pointer and knows the length of the array;
   * it also supplies a conversion operator to ArrayPtr.
   */
  template<typename T>
  class ScopedArray : public ArrayWithLength<T> {
  public:
    explicit ScopedArray(std::size_t n) : ArrayWithLength<T>(new T[n], n) {}
    ~ScopedArray() {delete [] this->m_ptr;}
  };
  
  template<typename T, unsigned N>
  class StaticArray : public ArrayWithLength<T> {
    T m_data[N];

  public:
    StaticArray() : ArrayWithLength<T>(m_data, N) {}
  };

#define PSI_STATIC_ARRAY_IMPL(n,params,inits) \
  template<typename T> class StaticArray<T, n> : public ArrayWithLength<T> { \
    T m_data[n]; \
    \
  public: \
    StaticArray() : ArrayWithLength<T>(m_data, n) {} \
    StaticArray params : ArrayWithLength<T>(m_data, n) {inits;} \
  };
  
  PSI_STATIC_ARRAY_IMPL(1, (const T& t1), (m_data[0]=t1))
  PSI_STATIC_ARRAY_IMPL(2, (const T& t1, const T& t2), (m_data[0]=t1,m_data[1]=t2))
  PSI_STATIC_ARRAY_IMPL(3, (const T& t1, const T& t2, const T& t3), (m_data[0]=t1,m_data[1]=t2,m_data[2]=t3))
  PSI_STATIC_ARRAY_IMPL(4, (const T& t1, const T& t2, const T& t3, const T& t4), (m_data[0]=t1,m_data[1]=t2,m_data[2]=t3,m_data[3]=t4))
  
#undef PSI_STATIC_ARRAY_IMPL

  template<typename T>
  class UniqueArray : public ArrayWithLength<T> {
  public:
    UniqueArray() : ArrayWithLength<T>() {}
    explicit UniqueArray(std::size_t n) : ArrayWithLength<T>(new T[n], n) {}
    ~UniqueArray() {
      if (this->m_ptr)
        delete [] this->m_ptr;
    }
    
    void reset() {
      delete [] this->m_ptr;
      this->m_ptr = 0;
      this->m_size = 0;
    }

    void reset(T *ptr, std::size_t n) {
      reset();
      this->m_ptr = ptr;
      this->m_size = n;
    }

    void reset(std::size_t n) {
      reset(new T[n], n);
    }

    T* release() {
      T *ptr = this->m_ptr;
      this->m_ptr = 0;
      this->m_size = 0;
      return ptr;
    }

    void swap(UniqueArray& o) {
      std::swap(this->m_ptr, o.m_ptr);
      std::swap(this->m_size, o.m_size);
    }
  };

  template<typename T, unsigned N>
  class SmallArray : public ArrayWithLength<T> {
    T m_data[N];
  public:
    SmallArray() {}
    explicit SmallArray(unsigned length) {resize(length);}
    ~SmallArray() {if (this->m_size > N) delete [] this->m_ptr;}
    
    SmallArray(const SmallArray& other) : ArrayWithLength<T>() {
      assign(other);
    }
    
    SmallArray(const ArrayWithLength<T>& other) {
      assign(other);
    }
    
    void resize(std::size_t new_size, const T& extend_value=T()) {
      if (this->m_size == new_size)
        return;
      
      if (new_size > N) {
        UniqueArray<T> new_ptr(new_size);
        if (this->m_size < new_size) {
          std::copy(this->m_ptr, this->m_ptr + this->m_size, new_ptr.get());
          std::fill(new_ptr.get() + this->m_size, new_ptr.get() + new_size, extend_value);
        } else {
          std::copy(this->m_ptr, this->m_ptr + new_size, new_ptr.get());
        }
        if (this->m_size > N)
          delete [] this->m_ptr;
        else
          std::fill(this->m_data, this->m_data + N, T());
        this->m_ptr = new_ptr.release();
      } else {
        if (this->m_size > N) {
          std::copy(this->m_ptr, this->m_ptr + new_size, this->m_data);
          delete [] this->m_ptr;
        }
        this->m_ptr = m_data;
        std::fill(this->m_data + new_size, this->m_data + N, extend_value);
      }
      
      this->m_size = new_size;
    }
    
    void assign(const ArrayWithLength<T>& src) {
      if (this->m_size != src.size()) {
        if (this->m_size > N) {
          delete [] this->m_ptr;
          this->m_ptr = 0;
          this->m_size = 0;
        }

        if (src.size() > N) {
          this->m_ptr = new T[src.size()];
        } else {
          this->m_ptr = m_data;
          std::fill(this->m_data + src.size(), this->m_data + N, T());
        }
        
        this->m_size = src.size();
      }
      std::copy(src.get(), src.get() + src.size(), this->get());
    }

    const SmallArray& operator = (const ArrayWithLength<T>& src) {
      assign(src);
      return *this;
    }

    const SmallArray& operator = (const SmallArray& src) {
      assign(src);
      return *this;
    }
  };
}

#endif
