#include "PoolGC.hpp"

namespace Psi {
  namespace GC {
    Pool::Pool(Pool *parent) : m_parent(parent), m_collector(0) {
    }

    Pool::~Pool() {
      collect();
      assert(m_nodes.empty());
    }

    void Pool::initialize_node(Node *node) {
      node->pool = this;
      node->n_refs = 0;
      node->gc_refs = 0;
      m_nodes.push_back(*node);
    }

    void Pool::collect() {
      // Use pool as collector ID
      void *collector = this;

      struct GCData {
        GCData(Pool *pool_) : pool(pool_) {}

        ~GCData() {
          assert(candidates.empty());
          assert(newly_reachable.empty());
        }

        GCData(const GCData&) = delete;
        GCData& operator = (const GCData&) = delete;

        // This can be defaulted when Boost.Intrusive gets rvalue ref
        // support.
        GCData(GCData&& src) : pool(src.pool) {
          candidates.swap(src.candidates);
          newly_reachable.swap(src.newly_reachable);
        }

        // This can be defaulted when Boost.Intrusive gets rvalue ref
        // support.
        GCData& operator = (GCData&& src) {
          std::swap(pool, src.pool);
          candidates.swap(src.candidates);
          newly_reachable.swap(src.newly_reachable);
          return *this;
        }

        Pool *pool;
        NodeList candidates;
        NodeList newly_reachable;
      };

      std::vector<GCData> data;

      for (Pool *pool = this; pool; pool = pool->m_parent) {
        assert(!pool->m_collector);
        pool->m_collector = collector;
        pool->m_collector_index = data.size();
        data.emplace_back(pool);
      }

      for (auto it = data.begin(); it != data.end(); ++it)
        it->pool->prepare_gc(collector, it->candidates);

      for (auto it = data.begin(); it != data.end(); ++it)
        it->pool->subtract_refs(collector, it->candidates);

      // First pass: find externally reachable nodes
      for (auto it = data.begin(); it != data.end(); ++it) {
        for (auto jt = it->candidates.begin(); jt != it->candidates.end(); ++jt) {
          if (jt->gc_refs != 0)
            it->newly_reachable.splice(it->newly_reachable.end(), it->candidates, jt);
        }
      }

      while(true) {
        NodeList reached;
        for (auto it = data.begin(); it != data.end(); ++it) {
          it->pool->restore_nodes(collector, it->newly_reachable, reached);
          assert(it->newly_reachable.empty());
        }

        if (reached.empty())
          break; // No new nodes found

        // Sort newly reached nodes into collector buckets
        while (!reached.empty()) {
          Node& n = reached.front();
          reached.pop_front();
          data[n.pool->m_collector_index].newly_reachable.push_back(n);
        }
      }

      // Clear references between nodes we're destroying
      for (auto it = data.begin(); it != data.end(); ++it)
        it->pool->clear_nodes(collector, it->candidates);

      // Destroy collected nodes
      for (auto it = data.begin(); it != data.end(); ++it) {
        it->pool->delete_nodes(it->candidates);
        assert(it->candidates.empty());
        assert(it->newly_reachable.empty());
      }

      // Reset collector attribute
      for (auto it = data.begin(); it != data.end(); ++it) {
        it->pool->m_collector = 0;
      }
    }

    NewPool::Base::~Base() {
    }
  }
}
