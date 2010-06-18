#ifndef HPP_PSI_POINTER_LIST
#define HPP_PSI_POINTER_LIST

#include <iterator>
#include <vector>

namespace Psi {
  /**
   * A container which gets its elements from a memory range.
   */
  template<typename T>
  class PointerList {
  public:
    typedef T value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T* pointer;
    typedef const T* const_pointer;

#ifdef __GLIBCXX__
    // Can make proper iterator classes this way if we're using libstdc++
    typedef __gnu_cxx::__normal_iterator<pointer, PointerList> iterator;
    typedef __gnu_cxx::__normal_iterator<const_pointer, PointerList> const_iterator;
#else
    typedef pointer iterator;
    typedef const_pointer const_iterator;
#endif

    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

    template<typename Alloc>
    PointerList(std::vector<T, Alloc>& v, size_type offset=0)
      : m_begin(&*v.begin() + offset), m_end(&*v.end()) {}
    template<typename Alloc>
    PointerList(const std::vector<typename std::remove_const<T>::type, Alloc>& v, size_type offset=0)
      : m_begin(&*v.begin() + offset), m_end(&*v.end()) {}
    template<typename Alloc>
    PointerList(const typename std::vector<T, Alloc>::iterator& b, decltype(b) e) : m_begin(&*b), m_end(&*e) {}
    template<typename Alloc>
    PointerList(const typename std::vector<typename std::remove_const<T>::type, Alloc>::iterator& b, decltype(b) e) : m_begin(&*b), m_end(&*e) {}
    PointerList(T *b, T *e) : m_begin(b), m_end(e) {}
    PointerList(T& el) : m_begin(&el), m_end(&el+1) {}

    iterator begin() {return iterator(m_begin);}
    iterator end() {return iterator(m_end);}
    const_iterator begin() const {return const_iterator(m_begin);}
    const_iterator end() const {return const_iterator(m_end);}

    reverse_iterator rbegin() {return reverse_iterator(end());}
    reverse_iterator rend() {return reverse_iterator(begin());}
    const_reverse_iterator rbegin() const {return const_reverse_iterator(end());}
    const_reverse_iterator rend() const {return const_reverse_iterator(begin());}

    size_type size() const {return m_end - m_begin;}
    bool empty() const {return m_begin == m_end;}

    reference front() {return *m_begin;}
    reference back() {return *(m_end - 1);}
    const_reference front() const {return *m_begin;}
    const_reference back() const {return *(m_end - 1);}

    reference operator [] (size_type n) {return m_begin[n];}
    const_reference operator [] (size_type n) const {return m_begin[n];}

    reference at(size_type n) {range_check(n); return m_begin[n];}
    const_reference at(size_type n) const {range_check(n); return m_begin[n];}

  private:
    void range_check(size_type n) {
      if (n >= size())
        throw std::out_of_range();
    }

    T *m_begin, *m_end;
  };
}

#endif
