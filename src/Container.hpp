#ifndef HPP_PSI_POINTER_LIST
#define HPP_PSI_POINTER_LIST

#include <iterator>
#include <stdexcept>
#include <vector>
#include <tr1/type_traits>

#include "Utility.hpp"

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
    PointerList(const std::vector<typename std::tr1::remove_const<T>::type, Alloc>& v, size_type offset=0)
      : m_begin(&*v.begin() + offset), m_end(&*v.end()) {}
    template<typename Alloc>
    PointerList(const typename std::vector<T, Alloc>::iterator& b,
		const typename std::vector<T, Alloc>::iterator e) : m_begin(&*b), m_end(&*e) {}
    template<typename Alloc>
    PointerList(const typename std::vector<typename std::tr1::remove_const<T>::type, Alloc>::iterator& b,
		const typename std::vector<typename std::tr1::remove_const<T>::type, Alloc>::iterator& e) : m_begin(&*b), m_end(&*e) {}
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

  template<typename T> class IntrusiveList;

  /**
   * Derive from this type to use #IntrusiveList.
   */
  template<typename T>
  class IntrusiveListNode : Noncopyable {
    friend class IntrusiveList<T>;

  public:
    IntrusiveListNode() : m_prev(NULL), m_next(NULL) {}
    ~IntrusiveListNode() {if (m_prev) IntrusiveList<T>::static_erase(this);}

  private:
    IntrusiveListNode<T> *m_prev, *m_next;
  };

  /**
   * \brief Intrusive doubly linked list.
   *
   * \c T needs to derive from IntrusiveListNode in order to be used
   * in this class.
   */
  template<typename T>
  class IntrusiveList : Noncopyable {
    friend class IntrusiveListNode<T>;

  public:
    template<typename U>
    class iterator_base : public std::iterator<std::bidirectional_iterator_tag, U> {
      friend class IntrusiveList;

    public:
      iterator_base() : m_node(NULL) {}

      U* operator -> () const {return static_cast<const U*>(m_node);}
      U& operator * () const {return *operator -> ();}

      const iterator_base& operator ++ () {m_node = m_node->m_next; return *this;}
      const iterator_base& operator -- () {m_node = m_node->m_prev; return *this;}
      iterator_base operator ++ (int) {iterator_base it(*this); ++(*this); return it;}
      iterator_base operator -- (int) {iterator_base it(*this); --(*this); return it;}

      template<typename W> bool operator == (const iterator_base<W>& o) const {return m_node == o.m_node;}
      template<typename W> bool operator != (const iterator_base<W>& o) const {return m_node != o.m_node;}

    private:
      iterator_base(IntrusiveListNode<T> *node) : m_node(node) {}

      IntrusiveListNode<T> *m_node;
    };

    typedef T value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef iterator_base<T> iterator;
    typedef iterator_base<const T> const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    IntrusiveList() {
      m_head.m_prev = &m_head;
      m_head.m_next = &m_head;
    }

    iterator begin() {return iterator(m_head.m_next);}
    const_iterator begin() const {return const_iterator(m_head.m_next);}
    iterator end() {return iterator(&m_head);}
    const_iterator end() const {return const_iterator(const_cast<IntrusiveListNode<T>*>(&m_head));}

    reverse_iterator rbegin() {return reverse_iterator(end());}
    const_reverse_iterator rbegin() const {return const_reverse_iterator(end());}
    reverse_iterator rend() {return reverse_iterator(begin());}
    const_reverse_iterator rend() const {return const_reverse_iterator(begin());}

    bool empty() const {return m_head.m_next == &m_head;}
    std::size_t size() const {return std::distance(begin(), end());}

    T& front() {return *begin();}
    const T& front() const {return *begin();}
    T& back() {return *rbegin();}
    const T& back() const {return *rbegin();}

    void insert(const iterator& position, T *value) {
      PSI_STATIC_ASSERT((std::tr1::is_base_of<IntrusiveListNode<T>, T>::value), "T does not inherit from IntrusiveListNode<T>");
      PSI_ASSERT(!linked(value), "values inserted into intrusive list was already in a list");

      IntrusiveListNode<T> *node = value;
      IntrusiveListNode<T> *pos = position.m_node;
      node->m_next = pos;
      node->m_prev = pos->m_prev;
      node->m_next->m_prev = node;
      node->m_prev->m_next = node;
    }

    T* erase(const iterator& position) {
      static_erase(position.m_node);
      return position.operator -> ();
    }

    void push_front(T *value) {insert(begin(), value);}
    void push_back(T *value) {insert(end(), value);}
    T* pop_front() {return erase(begin());}
    T* pop_back() {return erase(end());}

  private:
    IntrusiveListNode<T> m_head;

    static void static_erase(IntrusiveListNode<T> *node) {
      node->m_prev->m_next = node->m_next;
      node->m_next->m_prev = node->m_prev;
      node->m_prev = NULL;
      node->m_next = NULL;
    }

    static bool linked(IntrusiveListNode<T> *node) {
      return node->m_next;
    }
  };
}

#endif
