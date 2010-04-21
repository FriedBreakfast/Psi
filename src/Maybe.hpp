#ifndef HPP_PSI_MAYBE
#define HPP_PSI_MAYBE

#include <cassert>

namespace Psi {
  template<typename T>
  class Maybe {
  public:
    typedef T value_type;

    Maybe() : m_empty(true) {
    }

    Maybe(const Maybe& rhs) {
      m_empty = rhs.m_empty;
      if (!rhs.m_empty) {
        new (storage()) value_type (*rhs.storage());
      }
    }

    Maybe(Maybe&& rhs) {
      m_empty = rhs.m_empty;
      if (!rhs.m_empty) {
        new (storage()) value_type (std::move(*rhs.storage()));
      }
    }

    template<typename... Args>
    Maybe(Args&&... args) {
      m_empty = false;
      new (storage()) value_type (std::forward<Args>(args)...);
    }

    ~Maybe() {
      clear();
    }

    const Maybe& operator = (const Maybe& rhs) {
      if (!rhs.m_empty) {
        if (!m_empty) {
          *storage() = *rhs.storage();
        } else {
          new (storage()) value_type (*rhs.storage());
          m_empty = false;
        }
      } else {
        clear();
      }
      return *this;
    }

    const Maybe& operator = (Maybe&& rhs) {
      if (!rhs.m_empty) {
        if (!m_empty) {
          *storage = std::move(*rhs.storage());
        } else {
          new (storage()) value_type (std::move(*rhs.storage()));
          m_empty = false;
        }
      } else {
        clear();
      }
      return *this;
    }

    const Maybe& operator = (const value_type& rhs) {
      if (!m_empty) {
        *storage() = rhs;
      } else {
        new (storage()) value_type(rhs);
        m_empty = false;
      }
      return *this;
    }

    const Maybe& operator = (value_type&& rhs) {
      if (!m_empty) {
        *storage() = std::move(rhs);
      } else {
        new (storage()) value_type(std::move(rhs));
        m_empty = false;
      }
      return *this;
    }

    T* operator -> () {assert(!empty()); return storage();}
    const T* operator -> () const {assert(!empty()); return storage();}
    T& operator * () {assert(!empty()); return *storage();}
    const T& operator * () const {assert(!empty()); return *storage();}

    void clear() {
      if (!m_empty) {
        storage()->~value_type();
        m_empty = true;
      }
    }

    bool empty() const {
      return m_empty;
    }

    bool operator == (const Maybe& rhs) const {return compare(rhs, true, false, false, [](const value_type& x, const value_type& y) {return x==y;});}
    bool operator < (const Maybe& rhs) const {return compare(rhs, false, false, true, [](const value_type& x, const value_type& y) {return x<y;});}
    bool operator > (const Maybe& rhs) const {return compare(rhs, false, true, false, [](const value_type& x, const value_type& y) {return x>y;});}

    bool operator != (const Maybe& rhs) const {return !(*this == rhs);}
    bool operator >= (const Maybe& rhs) const {return !(*this < rhs);}
    bool operator <= (const Maybe& rhs) const {return !(*this > rhs);}

    friend bool operator == (const Maybe& lhs, const value_type& rhs) {return !lhs.empty() && (*lhs.storage() == rhs);}
    friend bool operator == (const value_type& lhs, const Maybe& rhs) {return rhs == lhs;}
    friend bool operator != (const Maybe& lhs, const value_type& rhs) {return !(lhs == rhs);}
    friend bool operator != (const value_type& lhs, const Maybe& rhs) {return !(rhs == lhs);}

  private:
    bool m_empty;
    typename std::aligned_storage<sizeof(value_type), alignof(value_type)>::type m_storage;

    value_type* storage() {
      return static_cast<value_type*>(static_cast<void*>(&m_storage));
    }

    const value_type* storage() const {
      return static_cast<const value_type*>(static_cast<const void*>(&m_storage));
    }

    template<typename F>
    bool compare(const Maybe& rhs, bool ee, bool fe, bool ef, const F& cmp) const {
      if (m_empty) {
        if (rhs.m_empty) {
          return ee;
        } else {
          return ef;
        }
      } else {
        if (rhs.m_empty) {
          return fe;
        } else {
          return cmp(*storage(), *rhs.storage());
        }
      }
    }
  };
}

#endif
