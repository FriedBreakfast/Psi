#ifndef HPP_PSI_TVM_OPERATIONMAP
#define HPP_PSI_TVM_OPERATIONMAP

#include "Core.hpp"

#include <tr1/unordered_map>

namespace Psi {
  namespace Tvm {
    /**
     * \brief Map for various properties of operations.
     */
    template<typename DestType>
    class OperationMap {
    private:
      Context *m_context;
      typedef std::tr1::unordered_map<const char*, DestType> ValueMap;
      ValueMap m_values;

    public:
      OperationMap(Context *context) : m_context(context) {
      }

      Context& context() const {
        return *m_context;
      }

      /**
       * Get the value associated with the given key, interning the
       * key in this maps context so get its canonical address.
       *
       * \return The value associated with the given key, or NULL if
       * the key is not present.
       */
      const DestType* get_ptr(const char *key) const {
        const char *interned = context().lookup_name(key);
        return interned ? get_ptr_interned(interned) : 0;
      }

      /**
       * Get the value associated with the given key, assuming the key
       * has already been interned in this maps context.
       */
      const DestType* get_ptr_interned(const char *key) const {
        PSI_ASSERT(key == context().lookup_name(key));
        typename ValueMap::const_iterator it = m_values.find(key);
        return (it != m_values.end()) ? &it->second : 0;
      }

    private:
      const DestType& check_deref_ptr(const DestType *ptr) {
        if (ptr) {
          return *ptr;
        } else {
          throw TvmInternalError("unknown operation name");
        }
      }

    public:
      const DestType& get(const char *key) const {
        return check_deref_ptr(get_ptr(key));
      }

      const DestType& get_interned(const char *key) const {
        return check_deref_ptr(get_ptr_interned(key));
      }

      void put(const char *key, const DestType& value) {
        const char *interned = context().lookup_name(key);
        if (!interned)
          throw TvmInternalError("unknown operation name");
        put_interned(interned, value);
      }

      void put_interned(const char *key, const DestType& value) {
        PSI_ASSERT(key == context().lookup_name(key));
        std::pair<typename ValueMap::iterator, bool> r = m_values.insert(std::make_pair(key, value));
        if (!r.second)
          throw TvmInternalError("Duplicate key in operation map");
      }
    };
  }
}

#endif
