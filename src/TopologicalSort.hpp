#ifndef HPP_PSI_TOPOLOGICAL_SORT
#define HPP_PSI_TOPOLOGICAL_SORT

#include <queue>
#include <stdexcept>
#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>

namespace Psi {
  /**
   * Perform a topological sort
   * 
   * \param first_it Iterator to first element in range to be sorted.
   * 
   * \param last_it Iterator to last element in range to be sorted.
   * 
   * \param ordering A list of ordering relations. The key comes first in the resulting sort,
   * and the value comes second. This is destroyed by the topological sort process.
   */
  template<typename T, typename U>
  void topological_sort(T first_it, T last_it, const U& ordering) {
    typedef typename T::value_type value_type;
    
    typedef boost::bimap<boost::bimaps::multiset_of<value_type>, boost::bimaps::multiset_of<value_type> > two_way_ordering_type;
    two_way_ordering_type two_way_ordering;
    for (typename U::const_iterator ii = ordering.begin(), ie = ordering.end(); ii != ie; ++ii)
      two_way_ordering.insert(typename two_way_ordering_type::value_type(ii->first, ii->second));
    
    std::queue<value_type> q;
    for (T i = first_it; i != last_it; ++i) {
      if (two_way_ordering.right.find(*i) == two_way_ordering.right.end())
        q.push(*i);
    }
    
    if (q.empty())
      throw std::runtime_error("topological sort failed: no possible ordering");
    
    T output = first_it;
    std::queue<value_type> small_q;
    while (!q.empty()) {
      *output = q.front();
      q.pop();
      
      typedef typename two_way_ordering_type::left_iterator left_iterator_type;
      std::pair<left_iterator_type, left_iterator_type> range = two_way_ordering.left.equal_range(*output);
      for (left_iterator_type i = range.first; i != range.second; ++i)
        small_q.push(i->second);
      two_way_ordering.left.erase(range.first, range.second);
      
      while (!small_q.empty()) {
        if (two_way_ordering.right.find(small_q.back()) == two_way_ordering.right.end())
          q.push(small_q.front());
        small_q.pop();
      }

      ++output;
    }
  }
}

#endif
