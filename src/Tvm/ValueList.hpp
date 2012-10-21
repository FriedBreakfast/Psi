#ifndef HPP_PSI_TVM_VALUELIST
#define HPP_PSI_TVM_VALUELIST

#include "Core.hpp"

#include <boost/iterator/iterator_facade.hpp>

namespace Psi {
  namespace Tvm {
    template<typename T, boost::intrusive::list_member_hook<> T::*member_hook>
    class ValueList {
      typedef boost::intrusive::list<T,
                                     boost::intrusive::member_hook<T, boost::intrusive::list_member_hook<>, member_hook>,
                                     boost::intrusive::constant_time_size<false> > BaseList;

      mutable BaseList m_base;

    public:
      class iterator
      : public boost::iterator_facade<iterator, const ValuePtr<T>, boost::bidirectional_traversal_tag> {
        typedef boost::iterator_facade<iterator, const ValuePtr<T>, boost::bidirectional_traversal_tag> BaseAdaptorType;

        ValuePtr<T> m_value_ptr;
        typename BaseList::iterator m_base, m_end;
        
      public:
        iterator() {}
        explicit iterator(typename BaseList::iterator base, typename BaseList::iterator end) : m_base(base), m_end(end) {reset_ptr();}
        
      private:
        friend class boost::iterator_core_access;
        
        void reset_ptr() {
          if (m_base != m_end)
            m_value_ptr.reset(&*m_base);
        }

        const ValuePtr<T>& dereference() const {
          return m_value_ptr;
        }
        
        bool equal(const iterator& y) const {
          return m_base == y.m_base;
        }
        
        void increment() {
          ++m_base;
          reset_ptr();
        }
        
        void decrement() {
          --m_base;
          reset_ptr();
        }
      };
      
      typedef iterator const_iterator;
      
      bool empty() const {return m_base.empty();}
      const_iterator begin() const {return const_iterator(m_base.begin(), m_base.end());}
      const_iterator end() const {return const_iterator(m_base.end(), m_base.end());}
      const_iterator iterator_to(const ValuePtr<T>& x) const {return const_iterator(m_base.iterator_to(*x), m_base.end());}
      std::size_t size() const {return m_base.size();}
      void swap(ValueList& other) {m_base.swap(other.m_base);}
      
      ValuePtr<T> at(std::size_t n) const {
        typename BaseList::iterator it = m_base.begin();
        std::advance(it, n);
        return ValuePtr<T>(&*it);
      }
      
      ValuePtr<T> front() const {return ValuePtr<T>(&m_base.front());}
      ValuePtr<T> back() const {return ValuePtr<T>(&m_base.back());}
      
      void insert(const ValuePtr<T>& ptr, T& elem) {
        m_base.insert(ptr ? m_base.iterator_to(*ptr) : m_base.end(), elem);
        intrusive_ptr_add_ref(&elem);
      }
      
      void erase(T& elem) {
        m_base.erase(m_base.iterator_to(elem));
        elem.list_release();
        intrusive_ptr_release(&elem);
      }
      
      void push_back(T& elem) {
        m_base.push_back(elem);
        intrusive_ptr_add_ref(&elem);
      }
      
      void insert(const ValuePtr<T>& ptr, const ValuePtr<T>& elem) {insert(ptr, *elem);}
      void erase(const ValuePtr<T>& elem) {erase(*elem);}
      void push_back(const ValuePtr<T>& elem) {push_back(*elem);}

      /**
       * \brief Check whether one element comes before another in this list.
       * 
       * Used by common_source() and source_dominated(), and shouldn't really be used
       * elsewhere.
       * 
       * Assumes that both items are members of this list.
       */
      bool before(const T& first, const T& second) const {
        for (typename BaseList::const_iterator ii = m_base.begin(), ie = m_base.end(); ii != ie; ++ii) {
          if (&*ii == &first)
            return true;
          else if (&*ii == &second)
            return false;
        }
        PSI_FAIL("Unreachable");
      }
      
    private:
      struct ElementDisposer {
        void operator () (T *v) {
          v->list_release();
          intrusive_ptr_release(v);
        }
      };

    public:
      void clear() {
        m_base.clear_and_dispose(ElementDisposer());
      }
      
      ~ValueList() {
        clear();
      }
    };
    
    template<typename V, typename T, boost::intrusive::list_member_hook<> T::* member, typename D>
    void visit_callback_impl(V& callback, const char *name, VisitorTag<ValueList<T, member> >, const D& values) {callback.visit_value_list(name, values);}
  }
}

#endif
