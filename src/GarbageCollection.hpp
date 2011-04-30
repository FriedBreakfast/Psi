#ifndef HPP_PSI_GARBAGE_COLLECTOR
#define HPP_PSI_GARBAGE_COLLECTON

#include "Assert.hpp"

#include <boost/utility/enable_if.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/noncopyable.hpp>
#include <boost/range/begin.hpp>
#include <boost/range/end.hpp>
#include <boost/range/iterator.hpp>

namespace Psi {
  template<typename T>
  class GCPtr : public boost::intrusive_ptr<T> {
    typedef boost::intrusive_ptr<T> BaseType;
  public:
    GCPtr() {}
    GCPtr(T *ptr) : BaseType(ptr) {}
    template<typename U> GCPtr(const GCPtr<U>& src) : BaseType(src.get()) {}
    template<typename U> GCPtr& operator = (const GCPtr<U>& src) {this->reset(src.get()); return *this;}
  };

  /**
   * \brief Dynamic cast wrapper for GCPtr.
   */
  template<typename T, typename U>
  GCPtr<T> dynamic_pointer_cast(const GCPtr<U>& ptr) {
    return GCPtr<T>(dynamic_cast<T*>(ptr.get()));
  }

  class GCPool;
  class GCVisitor;

  /**
   * \brief Base class for garbage collected types.
   */
  class GCBase : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink> > {
    friend class GCPool;
    friend class GCVisitor;

    std::size_t m_gc_ref_count;
    
    GCBase(const GCBase&);
    GCBase& operator = (const GCBase&);

  protected:
    virtual void gc_visit(GCVisitor&) = 0;
    virtual void gc_destroy() = 0;

  public:
    GCBase();
    virtual ~GCBase();

    friend void intrusive_ptr_add_ref(GCBase *ptr) {
      ++ptr->m_gc_ref_count;
    }

    friend void intrusive_ptr_release(GCBase *ptr) {
      if (!--ptr->m_gc_ref_count)
        ptr->gc_destroy();
    }
  };

  class GCPool {
  public:
    typedef boost::intrusive::list<GCBase, boost::intrusive::constant_time_size<false> > GCListType;

    GCPool();
    ~GCPool();
    void add(GCBase*);
    void collect();

  private:
    struct NonZeroRefCount;
    struct InsertDisposer;
    GCListType m_gc_list;
  };

  class GCVisitor : public boost::noncopyable {
    friend class GCPool;

    enum Mode {
      mode_decrement,
      mode_increment,
      mode_clear
    };

    Mode m_mode;
    GCPool::GCListType *m_gc_list;

    bool visit_ptr_internal(GCBase *ptr) const {
      switch (m_mode) {
      case mode_decrement:
        --ptr->m_gc_ref_count;
        return true;

      case mode_increment:
        if (!ptr->m_gc_ref_count) {
          ptr->unlink();
          m_gc_list->push_back(*ptr);
        }
        ++ptr->m_gc_ref_count;
        return true;

      case mode_clear:
        return false;

      default:
        PSI_FAIL("invalid mode flag");
      }
    }

    GCVisitor(Mode, GCPool::GCListType* =0);

    template<typename T>
    void mod_impl(T& x) {
      gc_visit(x, *this);
    }

    template<typename T>
    void mod_impl(GCPtr<T>& ptr) {
      visit_ptr(ptr);
    }

  public:
    template<typename T>
    void visit_ptr(GCPtr<T>& ptr) {
      if (!visit_ptr_internal(ptr.get()))
        ptr.reset();
    }

    template<typename T>
    void visit_range(T& range) {
      for (typename boost::range_iterator<T>::type ii = boost::begin(range), ie = boost::end(range); ii != ie; ++ii)
        mod_impl(*ii);
    }
    
    template<typename T>
    GCVisitor& operator % (T& x) {
      mod_impl(x);
      return *this;
    }
  };
}

#endif
