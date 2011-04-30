#include "GarbageCollection.hpp"

#include <boost/checked_delete.hpp>

namespace Psi {
  GCPool::GCPool() {
  }

  struct GCPoolReleaseDisposer {
    void operator () (GCBase *p) const {
      intrusive_ptr_release(p);
    }
  };

  GCPool::~GCPool() {
    // Must create a reference to every object so they don't get deleted when other pointers are cleared
    for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii)
      intrusive_ptr_add_ref(&*ii);

    GCVisitor clear_visitor(GCVisitor::mode_clear);
    for (GCListType::iterator ii = m_gc_list.begin(), ie = m_gc_list.end(); ii != ie; ++ii)
      ii->gc_visit(clear_visitor);

    m_gc_list.clear_and_dispose(GCPoolReleaseDisposer());
  }

  void GCPool::add(GCBase *ptr) {
    m_gc_list.push_back(*ptr);
  }

  struct GCPool::NonZeroRefCount {
    bool operator () (const GCBase& p) const {
      return p.m_gc_ref_count != 0;
    }
  };

  struct GCPool::InsertDisposer {
    GCListType *list;

    InsertDisposer(GCListType *list_) : list(list_) {}

    void operator () (GCBase *p) const {
      list->push_back(*p);
    }
  };

  void GCPool::collect() {
    GCListType clear_list;
    clear_list.swap(m_gc_list);
    GCVisitor dec_visitor(GCVisitor::mode_decrement);
    for (GCListType::iterator ii = clear_list.begin(), ie = clear_list.end(); ii != ie; ++ii)
      ii->gc_visit(dec_visitor);

    GCListType restore_list;
    clear_list.remove_and_dispose_if(NonZeroRefCount(), InsertDisposer(&restore_list));

    GCVisitor inc_visitor(GCVisitor::mode_increment, &restore_list);
    while (!restore_list.empty()) {
      GCListType::iterator b = restore_list.begin();
      b->gc_visit(inc_visitor);
      m_gc_list.splice(m_gc_list.end(), restore_list, b);
    }

    // Increment the reference counts on nodes to be deleted so that the reference counting
    // mechanism doesn't perform the deletion
    for (GCListType::iterator ii = clear_list.begin(), ie = clear_list.end(); ii != ie; ++ii)
      ++ii->m_gc_ref_count;

    // Clear internal pointers
    GCVisitor clear_visitor(GCVisitor::mode_clear);
    for (GCListType::iterator ii = clear_list.begin(), ie = clear_list.end(); ii != ie; ++ii)
      ii->gc_visit(clear_visitor);

    clear_list.clear_and_dispose(boost::checked_deleter<GCBase>());
  }

  GCVisitor::GCVisitor(Mode mode, GCPool::GCListType* gc_list)
  : m_mode(mode), m_gc_list(gc_list) {
  }

  GCBase::GCBase() : m_gc_ref_count(0) {
  }

  GCBase::~GCBase() {
  }

  void GCBase::gc_visit(GCVisitor&) {
  }
}
