#ifndef HPP_PSI_SHARED_MAP
#define HPP_PSI_SHARED_MAP

#include "Runtime.hpp"

namespace Psi {
  template<typename T, typename Comparator=std::less<T> >
  struct ThreeWayComparatorAdaptor : public std::binary_function<T, T, int> {
    typedef Comparator comparator_type;

    comparator_type comparator;

    ThreeWayComparatorAdaptor() {
    }

    ThreeWayComparatorAdaptor(const comparator_type& comparator_) : comparator(comparator_) {
    }

    int operator() (const T& first, const T& second) const {
      if (comparator(first, second)) {
        return -1;
      } else if (comparator(second, first)) {
        return 1;
      } else {
        return 0;
      }
    }
  };

  /**
    * An implementation of red-black trees using shared pointers to nodes
    * so that multiple trees can share some nodes. This costs some
    * efficiency in the case where only a single tree is used, but in the
    * case of multiple trees the cost of copy-plus-insert is also O(log
    * n) rather than O(n).
    *
    * \tparam KeyType Type of the key -only-; i.e. the type returned by
    * KeyFunction.
    *
    * \tparam ValueType Includes the whole value, including the key;
    * e.g. std::pair&lt;Key,Value&gt; for maps.
    *
    * \tparam KeyFunction Function to extract the key from the an
    * instance of ValueType. Must implement <tt>const KeyType&amp;
    * operator () (const ValueType&amp;)</tt>.
    *
    * \tparam Comparator Comparison function object type. This is a 3-way
    * comparator returning an <tt>int</tt> rather than a <tt>bool</tt>,
    * like <tt>strcmp</tt>.
    *
    * \tparam Allocator Allocator - must be an allocator for ValueType.
    *
    * \tparam PointerManager Pointer management type, i.e. garbage
    * collected or reference counted.
    */
  template<typename KeyType,
           typename ValueType,
           typename KeyFunction,
           typename Comparator>
  class SharedRbTree {
  public:
    typedef Comparator comparator_type;
    typedef KeyFunction key_function_type;
    typedef KeyType key_type;
    typedef ValueType value_type;

    SharedRbTree(const comparator_type& comparator=comparator_type(), const key_function_type& key_function=key_function_type())
    : m_size(0), m_comparator(comparator), m_key_function(key_function) {
    }

    /**
     * There is only a single lookup function which returns a const
     * value because data may be shared between trees; so a
     * replacement value must be put in using the insert function.
     */
    const value_type* lookup(const key_type& key) const {
      for (node_type *node = m_root.get(); node;) {
        int cmp = m_comparator(key, m_key_function(node->value));
        if (cmp < 0) {
          node = node->left.get();
        } else if (cmp > 0) {
          node = node->right.get();
        } else {
          return &node->value;
        }
      }

      return NULL;
    }
    
    /// \brief Number of elements in this tree
    std::size_t size() const {return m_size;}

    /**
     * Insert a value into the tree. If there is another value with the
     * same key, it is replaced.
     *
     * \return True if there was a previous value for the same key,
     * false otherwise.
     */
    bool insert(const value_type& value) {
      std::pair<bool, violation_type> result(node_insert(m_root, value));
      PSI_ASSERT(m_root.unique());
      m_root->color = black;
      ++m_size;
      return result.first;
    }
    
    /**
     * \brief Merge two trees.
     * 
     * At the moment this implementation looks rather inefficient, but I think
     * asymptotically this operation is O(m log (n+m)), where m is the size of the smaller
     * tree and n the larger.
     */
    void merge(const SharedRbTree& src) {
      if (m_size < src.m_size) {
        SharedRbTree tmp = src;
        if (m_root)
          tmp.merge_from(m_root);
        tmp.swap(*this);
      } else {
        if (src.m_root)
          merge_from(src.m_root);
      }
    }

    /**
     * Swap the contents of this tree with another.
     */
    void swap(SharedRbTree& src) {
      std::swap(m_size, src.m_size);
      std::swap(m_comparator, src.m_comparator);
      std::swap(m_key_function, src.m_key_function);
      m_root.swap(src.m_root);
    }

  private:
    enum color_type {
      red,
      black
    };

    struct node_type;

    typedef SharedPtr<node_type> NodePtr;

    struct node_type {
      color_type color;
      NodePtr left, right;
      value_type value;

      node_type(const value_type& value_) : value(value_) {}

      void pointer_clear() {
        left.reset();
        right.reset();
      }
    };

    std::size_t m_size;
    comparator_type m_comparator;
    key_function_type m_key_function;
    NodePtr m_root;

    NodePtr create_node(color_type color, const value_type& value, const NodePtr& left=NodePtr(), const NodePtr& right=NodePtr()) {
      NodePtr ptr(new node_type(value));
      ptr->color = color;
      ptr->left = left;
      ptr->right = right;
      return ptr;
    }

    enum violation_type {
      violation_left,
      violation_right,
      violation_red,
      violation_none
    };

    std::pair<bool, violation_type> node_insert(NodePtr& node, const value_type& value) {
      if (node) {
        int cmp = m_comparator(m_key_function(value), m_key_function(node->value));
        if (cmp == 0) {
          if (node.unique()) {
            node->value = value;
          } else {
            node = create_node(node->color, value, node->left, node->right);
          }
          return std::make_pair(true, violation_none);
        } else {
          if (!node.unique())
            node = create_node(node->color, node->value, node->left, node->right);

          if (cmp < 0) {
            std::pair<bool, violation_type> result(node_insert(node->left, value));
            return std::make_pair(result.first, rebalance(node, true, result.second));
          } else {
            std::pair<bool, violation_type> result(node_insert(node->right, value));
            return std::make_pair(result.first, rebalance(node, false, result.second));
          }
        }
      } else {
        node = create_node(red, value);
        return std::make_pair(false, violation_red);
      }
    }

    violation_type rebalance(NodePtr& node, bool left_updated, violation_type violation) {
      PSI_ASSERT(node.unique());

      if (violation == violation_none) {
        return violation_none;
      } else if (violation == violation_red) {
        return node->color == red ? (left_updated ? violation_left : violation_right) : violation_none;
      } else {
        PSI_ASSERT(node->color == black);

        if (left_updated) {
          PSI_ASSERT(node->left);
          PSI_ASSERT(node->left.unique());

          if (node->right && node->right->color == red) {
            node->color = red;
            node->left->color = black;
            node->right->color = black;
            return violation_red;
          } else if (violation == violation_left) {
            PSI_ASSERT(node->left->left);
            PSI_ASSERT(node->left->left.unique());
            PSI_ASSERT(node->left->left->color == red);

            NodePtr left(node->left);

            node->color = red;
            node->left = left->right;
            left->color = black;
            left->right = node;

            node = left;
            return violation_none;
          } else {
            PSI_ASSERT(violation == violation_right);
            PSI_ASSERT(node->left->right);
            PSI_ASSERT(node->left->right.unique());
            PSI_ASSERT(node->left->right->color == red);

            NodePtr left_right(node->left->right);

            node->color = red;
            left_right->color = black;

            node->left->right = left_right->left;
            left_right->left = node->left;
            node->left = left_right->right;
            left_right->right = node;

            node = left_right;
            return violation_none;
          }
        } else {
          PSI_ASSERT(node->right && node->right.unique());

          if (node->left && node->left->color == red) {
            node->color = red;
            node->left->color = black;
            node->right->color = black;
            return violation_red;
          } else if (violation == violation_right) {
            PSI_ASSERT(node->right->right);
            PSI_ASSERT(node->right->right.unique());
            PSI_ASSERT(node->right->right->color == red);

            NodePtr right(node->right);

            node->color = red;
            node->right = right->left;
            right->color = black;
            right->left = node;

            node = right;
            return violation_none;
          } else {
            PSI_ASSERT(violation == violation_left);
            PSI_ASSERT(node->right->left);
            PSI_ASSERT(node->right->left.unique());
            PSI_ASSERT(node->right->left->color == red);

            NodePtr right_left(node->right->left);

            node->color = red;
            right_left->color = black;

            node->right->left = right_left->right;
            right_left->right = node->right;
            node->right = right_left->left;
            right_left->left = node;

            node = right_left;
            return violation_none;
          }
        }
      }
    }
    
    void merge_from(const NodePtr& node) {
      insert(node->value);
      if (node->left)
        merge_from(node->left);
      if (node->right)
        merge_from(node->right);
    }
  };

  /**
   * \brief A map which can be duplicated in O(1) by sharing nodes.
   */
  template<typename K, typename V, typename Cmp=std::less<K> >
  class SharedMap {
  public:
    typedef K key_type;
    typedef V mapped_type;
    typedef std::pair<key_type, mapped_type> value_type;

  private:
    struct Get1st {
      const key_type& operator () (const value_type& v) const {
        return v.first;
      }
    };
    
    SharedRbTree<key_type, value_type, Get1st, ThreeWayComparatorAdaptor<key_type, Cmp> > m_tree;
    
  public:
    bool insert(const value_type& value) {return m_tree.insert(value);}
    bool put(const key_type& key, const mapped_type& value) {return m_tree.insert(value_type(key, value));}
    const mapped_type* lookup(const key_type& key) const {const value_type *ptr = m_tree.lookup(key); return ptr ? &ptr->second : NULL;}
  };
  
  /**
   * \brief A set which can be duplicated in O(1) by sharing nodes.
   */
  template<typename V, typename Cmp=std::less<V> >
  class SharedSet {
  public:
    typedef V value_type;
    
  private:
    struct Forward {
      const value_type& operator () (const value_type& v) const {
        return v;
      }
    };
    
    SharedRbTree<value_type, value_type, Forward, ThreeWayComparatorAdaptor<value_type, Cmp> > m_tree;

  public:
    bool insert(const value_type& v) {return m_tree.insert(v);}
    bool contains(const value_type& v) {return m_tree.lookup(v);}
    void merge(const SharedSet& src) {m_tree.merge(src.m_tree);}
  };
}

#endif
