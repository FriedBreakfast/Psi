#ifndef PSI_POOL_GC_HPP
#define PSI_POOL_GC_HPP

#include <boost/intrusive/list.hpp>
#include <boost/intrusive_ptr.hpp>

namespace Psi {
  namespace GC {
    class Pool;

    class Node {
      friend class Pool;
      template<typename Derived> friend class PoolBase;

      friend void intrusive_ptr_add_ref(Node *node) {
	++node->n_refs;
      }

      friend void intrusive_ptr_release(Node *node) {
	if (--node->n_refs == 0)
	  node->release_private();
      }

    public:
      Node() : pool(0), n_refs(0), gc_refs(0) {}
      Node(const Node&) = delete;
      Node& operator = (const Node&) = delete;

    private:
      Pool *pool;
      std::size_t n_refs;
      std::size_t gc_refs;
      boost::intrusive::list_member_hook<> list_hook;

      void release_private();
    };

    class Pool {
    public:
      Pool(Pool *parent);
      Pool(const Pool&) = delete;
      Pool(Pool&&) = delete;
      ~Pool();

      void collect();

    protected:
      void initialize_node(Node *n);

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
      boost::intrusive_ptr<T> m_ptr;
    };

    class NewPoolBase : public Node {
    public:
      virtual ~NewPoolBase();
      virtual void gc_visit(const std::function<bool(Node*)>& visitor) = 0;
    };

    class NewPool : public PoolBase<NewPool> {
      friend class PoolBase<NewPool>;

    public:
      template<typename T, typename... Args>
      GCPtr<T> new_(Args&&... args) {
        static_assert((std::is_base_of<NewPoolBase, T>::value), "T must derive Base");
        GCPtr<T> ptr(new T(std::forward<Args>(args)...));
        initialize_node(ptr.get());
        return ptr;
      }

    private:
      template<typename F>
      void visit(Node *node, F&& visitor) {
        static_cast<NewPoolBase*>(node)->gc_visit(visitor);
      }

      void destroy(Node *node) {
        delete static_cast<NewPoolBase*>(node);
      }
    };

    /**
     * A specialization of #NewPool which is interface-compatible with
     * #TypePool.
     */
    template<typename T>
    class TypedNewPool : public NewPool {
    public:
      static_assert((std::is_base_of<NewPoolBase, T>::value), "T must derive NewPoolBase");

      template<typename... Args>
      GCPtr<T> new_(Args&&... args) {
        return NewPool::new_<T>(std::forward<Args>(args)...);
      }
    };

    struct ADLVisitor {
      template<typename T, typename F>
      void operator () (T& object, F&& visitor) const {
        gc_visit(object, std::forward<F>(visitor));
      }
    };

    class TypePoolBase : public Node {
    public:
      template<typename T, typename U> friend class TypePool;

    private:
      void *block;
    };

    template<typename T, typename Visitor=ADLVisitor>
    class TypePool : public PoolBase<TypePool<T, Visitor> > {
      union BlockEntry {
        typename std::aligned_storage<sizeof(T), alignof(T)>::type used;
        struct {
          BlockEntry *next;
        } empty;
      };

      struct Block : boost::intrusive::list_base_hook<> {
        std::size_t block_size;
        std::size_t used_count;
        BlockEntry *free;
        BlockEntry entries[1];
      };

    public:
      typedef T ElementType;
      typedef Visitor VisitorType;

      friend class PoolBase<TypePool>;

      static_assert((std::is_base_of<TypePoolBase, T>::value), "T must derive Base");

      static const std::size_t default_block_size = 1024;
      static const std::size_t default_max_free = 1024;

      TypePool(std::size_t block_size=default_block_size,
               std::size_t max_free=default_max_free)
        : m_block_size(block_size),
          m_max_free(max_free),
          m_total_free(0) {}

      TypePool(Visitor visitor,
               std::size_t block_size=default_block_size,
               std::size_t max_free=default_max_free)
        : m_block_size(block_size),
          m_max_free(max_free),
          m_total_free(0),
          m_visitor(std::move(visitor)) {}

      template<typename... Args>
      GCPtr<T> new_(Args&&... args) {
        auto storage = allocate_storage();
        T *ptr;
        try {
          ptr = new (storage.second) T (std::forward<Args>(args)...);
        } catch (...) {
          destroy_storage(storage.first, storage.second);
          throw;
        }
        initialize_node(ptr, this);
	ptr->block = storage.first;
        return GC::GCPtr<T>(ptr);
      }

    private:
      std::size_t m_block_size;
      std::size_t m_max_free;

      std::size_t m_total_free;
      Visitor m_visitor;
      boost::intrusive::list<Block, boost::intrusive::constant_time_size<false> > m_blocks;

      template<typename F>
      void visit(Node *node, F&& visitor) {
        m_visitor(static_cast<T*>(node), std::forward<F>(visitor));
      }

      void destroy(Node *node) {
        T *t = static_cast<T*>(node);
        Block *block = static_cast<Block*>(t->block);
        t->~T();
        BlockEntry *entry = static_cast<BlockEntry*>(t);
        destroy_storage(block, entry);
      }

      std::pair<Block*,BlockEntry*> allocate_storage() {
        if (m_blocks.front().free) {
          Block *block = &m_blocks.front();
          BlockEntry *entry = m_blocks.front().free;
          if (!entry->free.next) {
            // Move block to the end of the block list since it has no
            // more free slots
            m_blocks.splice(m_blocks.end(), m_blocks, m_blocks.begin());
          }
          return std::make_pair(block, entry);
        } else {
          // Allocate a new block
          void *storage = new char[sizeof(Block) + sizeof(BlockEntry) * (m_block_size-1)];
          Block *block = new (storage) Block;
          block->pool = this;
          block->block_size = m_block_size;
          block->used_count = 1;
          block->free = block->entries + 2;

          for (std::size_t i = 2; i < m_block_size; ++i)
            block->entries[i-1].free.next = block->entries + i;
          block->entries[m_block_size-1].free.next = 0;

          m_blocks.push_front(*block);

          return std::make_pair(block, block->entries);
        }
      }

      void destroy_storage(Block *block, BlockEntry *entry) {
        ++m_total_free;
        if ((--block->used_count == 0) && (m_max_free < m_total_free - block->block_size)) {
          m_total_free -= block->block_size;
          m_blocks.erase(m_blocks.iterator_to(*block));
          delete block;
        } else {
          if (!block->free) {
            // If block had no free elements previously, move it to
            // the front of the block list so the next allocated
            // element comes from it
            m_blocks.splice(m_blocks.begin(), m_blocks, m_blocks.iterator_to(*block));
          }
          entry->free.next = block->free;
          block->free = entry;
        }
      }
    };
  }
}

#endif
