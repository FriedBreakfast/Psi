#ifndef HPP_PSI_GARBAGE_COLLECTOR
#define HPP_PSI_GARBAGE_COLLECTOR

#include "Assert.hpp"

namespace Psi {
  class GCVisitorIncrement {
  };

  class GCVisitorDecrement {
  };

  /**
   * \brief Garbage collect a set of objects.
   *
   * \param accessor Accessor to the objects.
   *
   * This must have the following functions:
   *
   * <dl>
   * <dt><tt>template&lt;typename Visitor&gt; void visit(T*, Visitor&)</tt></dt><dd></dd>
   * <dt><tt>T*& next(T*)</tt></dt><dd>Get the next object in the list</dd>
   * <dt><tt>T*& prev(T*)</tt></dt><dd>Get the previous object in the list</dd>
   * <dt><tt>void destroy(T*)</tt></dt><dd>Destroy an object. This should not release any references held by the object.</dd></dt>
   * </dl>
   */
  template<typename T, typename U>
  T* garbage_collect(T* objects, const U& accessor) {
    GCVisitorDecrement dec_visitor;
    for (T *i = objects; i; i = accessor.next(i))
      accessor.visit(i, dec_visitor);

    T *reachable_list = 0, *unreachable_list = 0;
    for (T *cur = objects, *next; cur; cur = next) {
      next = accessor.next(objects);
      if (accessor.refcount(cur)) {
        accessor.next(cur) = reachable_list;
        reachable_list = cur;
      } else {
        accessor.next(cur) = unreachable_list;
        if (unreachable_list)
          accessor.prev(unreachable_list) = cur;
        unreachable_list = cur;
      }
    }

    if (unreachable_list)
      accessor.prev(unreachable_list) = 0;

    T *result_list = 0;
    GCVisitorIncrement inc_visitor(&reachable_list);
    while (reachable_list) {
      T *ptr = reachable_list;
      reachable_list = accessor.next(reachable_list);
      accessor.visit(ptr, inc_visitor);

      accessor.next(ptr) = result_list;
      if (result_list)
        accessor.prev(result_list) = ptr;
      result_list = ptr;
    }

    // Increment the reference counts on nodes to be deleted so that the reference counting
    // mechanism doesn't perform the deletion
    for (T *cur = unreachable_list, *next; cur; cur = next) {
      next = accessor.next(cur);
      accessor.destroy(cur);
    }
  }
}

#endif
