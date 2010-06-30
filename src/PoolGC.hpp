#ifndef PSI_POOL_GC_HPP
#define PSI_POOL_GC_HPP

#include <atomic>

#include <boost/intrusive/list.hpp>

#include "IntrusivePtr.hpp"

namespace Psi {
  namespace GC {
    class Pool;

    class Node {
      friend class Pool;
      template<typename Derived> friend class PoolBase;

      friend void node_add_ref(Node*);
      friend void node_release(Node*);

    private:
      Pool *pool;
      std::size_t n_refs;
      std::size_t gc_refs;
      boost::intrusive::list_member_hook<> list_hook;
    };

    void node_release_private(Node *node);

    inline void node_add_ref(Node *node) {
      ++node->n_refs;
    }

    inline void node_release(Node *node) {
      if (--node->n_refs == 0)
        node_release_private(node);
    }

    class Pool {
    public:
      Pool(Pool *parent);
      Pool(const Pool&) = delete;
      Pool(Pool&&) = delete;
      ~Pool();

      void collect();

    protected:
      void insert(Node *n);

      typedef boost::intrusive::list<Node,
                                     boost::intrusive::member_hook<Node, boost::intrusive::list_member_hook<>, &Node::list_hook>,
                                     boost::intrusive::constant_time_size<false> > NodeList;
      NodeList m_nodes;
      Pool *m_parent;
      void *m_collector;
      std::size_t m_collector_index;

    private:
      virtual void prepare_gc(void *collector, NodeList& gc_list) = 0;
      virtual void subtract_refs(void *collector, NodeList& gc_list) = 0;
      virtual void restore_nodes(void *collector, NodeList& restore_list, NodeList& reached_list) = 0;
      virtual void clear_nodes(void *collector, NodeList& collected) = 0;
      virtual void delete_nodes(NodeList& collected) = 0;
    };

    template<typename Derived>
    class PoolBase : public Pool {
    private:
      Derived* derived() {
        return static_cast<Derived*>(this);
      }

      /**
       * Prepare garbage collection run:
       *
       * - Move all elements in this pool into a different list.
       * - Copy reference counts from \c n_refs to \c gc_refs.
       * - Set collector attribute.
       *
       * \param collector Collector ID.
       * \param candidates List to place GC candidates into
       */
      virtual void prepare_gc(void *collector, NodeList& candidates) {
        candidates.splice(candidates.end(), m_nodes);

        for (auto it = candidates.begin(); it != candidates.end(); ++it)
          it->gc_refs = it->n_refs;
      }

      /**
       * Subtract reference counts.
       *
       * \param collector Collector ID.
       * \param candidates List of nodes we are subtracting from.
       */
      virtual void subtract_refs(void *collector, NodeList& candidates) {
        for (auto it = candidates.begin(); it != candidates.end(); ++it) {
          derived()->visit(&*it, [collector] (Node *t) -> bool {
            if (t && (t->pool->m_collector == collector))
              --t->gc_refs;
            return true;
          });
        }
      }

      /**
       * Restore reference counts of some nodes which have been found
       * to be reached, and return them to the list of nodes owned by
       * this pool.
       *
       * \param collector Collector ID.
       * \param restore_list List of nodes whose references we are restoring.
       * \param reached_list List which newly reached objects should be inserted into.
       */
      virtual void restore_nodes(void *collector, NodeList& restore_list, NodeList& reached_list) {
        for (auto it = restore_list.begin(); it != restore_list.end(); ++it) {
          derived()->visit(&*it, [this, collector, &reached_list] (Node *t) -> bool {
            if (t && (t->pool->m_collector == collector) && (t->gc_refs == 0)) {
              // Move back into the collected set
              t->gc_refs = 1;
              t->list_hook.unlink();
              reached_list.push_back(*t);
            }
            return true;
          });
        }
        m_nodes.splice(m_nodes.end(), restore_list);
      }

      /**
       * Clear pointers to other nodes being collected from collected
       * nodes, to prevent invalid access when destructors are run.
       */
      virtual void clear_nodes(void *collector, NodeList& collected) {
        for (auto it = collected.begin(); it != collected.end(); ++it) {
          derived()->visit(&*it, [collector] (Node *t) -> bool {
            return !(t && ((t->pool->m_collector == collector) && (t->gc_refs == 0)));
          });
        }
      }

      /**
       * Free collected nodes.
       */
      virtual void delete_nodes(NodeList& collected) {
        while (!collected.empty()) {
          Node *n = &collected.front();
          collected.pop_front();
          derived()->destroy(n);
        }
      }
    };

    template<typename T>
    class GCPtr : public std::iterator_traits<T*> {
    public:
      GCPtr() : m_ptr(0) {}
      explicit GCPtr(T *ptr) : m_ptr(ptr) {}
      GCPtr(GCPtr&&) = default;
      template<typename U> GCPtr(GCPtr<U>&& src) : m_ptr(std::move(src.m_ptr)) {}
      template<typename U> GCPtr(const GCPtr<U>& src) : m_ptr(src.m_ptr) {}
      template<typename U> GCPtr& operator = (const GCPtr<U>& src) {m_ptr = src.m_ptr; return *this;}
      GCPtr& operator = (GCPtr&& src) {m_ptr = std::move(src.m_ptr); return *this;}
      template<typename U> GCPtr& operator = (GCPtr<U>&& src) {m_ptr = std::move(src.m_ptr); return *this;}
      T* get() const {return m_ptr.get();}
      T& operator * () const {return *m_ptr.get();}
      T* operator -> () const {return m_ptr.get();}
      T* release() {return m_ptr.release();}

      explicit operator bool () const {return m_ptr;}

      template<typename F>
      void gc_visit(F&& f) {
        if (!f(m_ptr.get()))
          m_ptr.release();
      }

    private:
      IntrusivePtr<T> m_ptr;
    };

    class NewPool : public PoolBase<NewPool> {
      friend class PoolBase<NewPool>;

    public:
      class Base : public Node {
      public:
        friend class NewPool;
        template<typename T> friend class GCPtr;

        virtual ~Base();
        virtual void gc_visit(const std::function<bool(Node*)>& visitor) = 0;

        friend void intrusive_ptr_add_ref(Base *p) {
          node_add_ref(p);
        }

        friend void intrusive_ptr_release(Base *p) {
          node_release(p);
        }
      };

      template<typename T, typename... Args>
      GCPtr<T> new_(Args&&... args) {
        GCPtr<T> ptr(new T(std::forward<Args>(args)...));
        Base *base = ptr.get();
        insert(base);
        return ptr;
      }

    private:
      template<typename F>
      void visit(Node *node, F&& visitor) {
        static_cast<Base*>(node)->gc_visit(visitor);
      }

      void destroy(Node *node) {
        delete static_cast<Base*>(node);
      }
    };
  }
}

#endif
