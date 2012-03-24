#ifndef HPP_PSI_VISITOR
#define HPP_PSI_VISITOR

#include <vector>
#include <utility>

#include <boost/array.hpp>
#include <boost/type_traits/remove_cv.hpp>

/**
 * \file
 * 
 * Visitor pattern implementation, designed to allow visiting multiple
 * objects of the same type at once. This supports the following automatically:
 *
 * <ul>
 * <li>Hashing</li>
 * <li>Comparison</li>
 * <li>Serialization</li>
 * <li>Duplication</li>
 * <li>Garbage collection</li>
 * </ul>
 */

namespace Psi {
  /**
   * Type used in overloads of \c visit
   */
  template<typename T> class VisitorTag {
    typedef T type;
  };

  template<typename T>
  VisitorTag<typename boost::remove_cv<T>::type> visitor_tag() {
    return VisitorTag<typename boost::remove_cv<T>::type>();
  };

  /**
   * Free function used to visit an object.
   *
   * By default, this is equivalent to T::visit.
   */
  template<typename V, typename T>
  void visit(V& v, VisitorTag<T>) {
    T::visit(v);
  }

  template<typename V, typename A, typename B>
  void visit(V& v, VisitorTag<PSI_STD::pair<A,B> >) {
    v("first", &std::pair<A,B>::first)
    ("second", &std::pair<A,B>::second);
  }

  /**
   * Callback used to visit a base class.
   */
  template<typename T, typename V>
  void visit_base(V& v) {
    visit(v, VisitorTag<T>());
  }

  template<typename V, typename T, typename D>
  void visit_callback(V& callback, const char *name, VisitorTag<T>, const D& objects) {callback.visit_object(name, objects);}

  template<typename V, typename D>
  void visit_callback(V& callback, const char *name, VisitorTag<bool>, const D& values) {callback.visit_simple(name, values);}
  template<typename V, typename D>
  void visit_callback(V& callback, const char *name, VisitorTag<char>, const D& values) {callback.visit_simple(name, values);}
  template<typename V, typename D>
  void visit_callback(V& callback, const char *name, VisitorTag<unsigned>, const D& values) {callback.visit_simple(name, values);}

  template<typename V, typename T, typename A, typename D>
  void visit_callback(V& callback, const char *name, VisitorTag<PSI_STD::vector<T,A> >, const D& values) {callback.visit_sequence(name, values);}

  template<typename V, typename K, typename O, typename C, typename A, typename D>
  void visit_callback(V& callback, const char *name, VisitorTag<PSI_STD::map<K, O, C, A> >, const D& values) {callback.visit_map(name, values);}

  template<typename V, typename T, std::size_t N>
  void visit_callback(V& callback, const char *name, const boost::array<T*,N>& values) {
#ifdef PSI_DEBUG
    for (std::size_t i = 0; i != N; ++i)
      PSI_ASSERT(values[i]);
#endif
    visit_callback(callback, name, visitor_tag<T>(), values);
  }

  /**
   * If A is const, gives const B, else gives B.
   */
  template<typename A, typename B>
  struct CopyConst {
    typedef B type;
  };

  template<typename A, typename B>
  struct CopyConst<const A, B> {
    typedef const B type;
  };

  template<typename ObjectType, typename Callback, std::size_t N>
  class ObjectVisitor {
    ObjectType *const* m_objects;
    Callback *m_callback;
    
  public:
    typedef ObjectVisitor<ObjectType, Callback, N> ThisType;
    static const std::size_t arity = N;

    ObjectVisitor(Callback *callback, ObjectType *const* objects)
    : m_objects(objects), m_callback(callback) {
    }
    
    template<typename Base, typename U>
    ThisType& operator () (const char *name, U Base::* member) {
      boost::array<typename CopyConst<ObjectType, U>::type*, arity> values;
      for (std::size_t ii = 0; ii != arity; ++ii)
        values[ii] = &(m_objects[ii]->*member);
      visit_callback(*m_callback, name, values);
      return *this;
    }
  };

  template<typename T, typename U, std::size_t N>
  void visit_members(T& visitor, const boost::array<U*, N>& objects) {
    ObjectVisitor<U, T, N> ov(&visitor, &objects[0]);
    visit(ov, visitor_tag<U>());
  }
}

#endif
