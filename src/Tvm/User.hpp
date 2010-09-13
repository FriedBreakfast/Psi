#ifndef HPP_PSI_USER
#define HPP_PSI_USER

#include <iterator>
#include <cstddef>
#include <tr1/cstdint>
#include <tr1/type_traits>

#include "../Utility.hpp"

/**
 * \file
 *
 * A similar idea to LLVMs User/Use class.
 */

namespace Psi {
  class User;
  class Used;

  /**
   * \brief Internal class for implemented #User and #Used.
   *
   * Do not use this class directly outside of those two classes.
   */
  class Use {
  public:
    enum UseMode {
      UserHeadInline = 1,
      UserHeadMalloc = 2,
      UsedHead = 3
    };

    Use() {}

    void init_user_head(bool is_inline, User *owner, std::size_t n_uses);
    void init_use_node();
    void init_used_head();

    bool user_head() {return (m_target == UserHeadInline) || (m_target == UserHeadMalloc);}
    bool used_head() {return m_target == UsedHead;}
    bool use_node() {return (m_target == 0) || (m_target > UsedHead);}

    User *owner() {PSI_ASSERT(user_head()); return m_rest.head.owner;}
    std::size_t n_uses() {PSI_ASSERT(user_head()); return m_rest.head.n_uses;}

    std::pair<User*, std::size_t> locate_owner() {
      PSI_ASSERT(use_node());

      Use *u = this;
      std::size_t index = 0;
      while (true) {
	--u;
	if (u->user_head())
	  return std::make_pair(u->m_rest.head.owner, index);
	++index;
      }
    }

    Use *next() {PSI_ASSERT(!user_head()); return m_rest.use.next;}
    Use *prev() {PSI_ASSERT(!user_head()); return m_rest.use.prev;}
    Used *target() {PSI_ASSERT(use_node()); return reinterpret_cast<Used*>(m_target);}

    // "use_node" operations
    void set_target(Used *target);

    // "used_head" operations
    void clear_users();
    void replace_with(Used *target);

  private:
    std::tr1::intptr_t m_target;
    Use(const Use&);

    union {
      struct {
	Use *next;
	Use *prev;
      } use;
      struct {
	User *owner;
	std::size_t n_uses;
      } head;
    } m_rest;
  };

  class UserIterator {
    friend class Used;

  public:
    typedef std::bidirectional_iterator_tag iterator_category;

    UserIterator() : m_use(0), m_user(0) {}

    bool operator == (const UserIterator& o) const {return m_use == o.m_use;}
    bool operator != (const UserIterator& o) const {return m_use != o.m_use;}

    User& operator * () const {return *get();}
    User* operator -> () const {return get();}

    User* get() const {
      if (!m_user) {
	std::pair<User*, std::size_t> p = m_use->locate_owner();
	m_user = p.first;
	m_use_index = p.second;
      }
      return m_user;
    }

    std::size_t use_index() const {get(); return m_use_index;}

    const UserIterator& operator ++ () {m_user = 0; m_use = m_use->next(); return *this;}
    const UserIterator& operator -- () {m_user = 0; m_use = m_use->prev(); return *this;}
    UserIterator operator ++ (int) {UserIterator tmp(*this); operator ++ (); return tmp;}
    UserIterator operator -- (int) {UserIterator tmp(*this); operator -- (); return tmp;}

  private:
    UserIterator(Use *use) : m_use(use), m_user(0) {}

    Use *m_use;
    mutable User *m_user;
    mutable std::size_t m_use_index;
 };

  class Used : public CheckedCastBase {
    friend class Use;

  private:
    Used(const Used&);
    Use m_use;

  protected:
    Used();
    ~Used();

    UserIterator users_begin() {return UserIterator(m_use.next());}
    UserIterator users_end() {return UserIterator(&m_use);}

    bool is_used() {return m_use.next() != &m_use;}
    void replace_with(Used *target) {m_use.replace_with(target);}
  };

  template<std::size_t N>
  class StaticUses {
    friend class UserInitializer;

  private:
    Use uses[N+1];
  };

  class UserInitializer {
    friend class User;

  public:
    template<std::size_t N>
    UserInitializer(StaticUses<N>& uses)
      : m_n_uses(N), m_uses(uses.uses) {
    }

    UserInitializer(std::size_t n_uses, Use *uses)
      : m_n_uses(n_uses), m_uses(uses) {
    }

    std::size_t n_uses() const {return m_n_uses;}

  private:
    std::size_t m_n_uses;
    Use *m_uses;
  };

  class User {
  private:
    User(const User&);

    Use *m_uses;

    Use& use_n(std::size_t n) const {
      PSI_ASSERT(m_uses && (n < m_uses[0].n_uses()));
      return m_uses[n+1];
    }

  protected:
    User(const UserInitializer& ui);
    virtual ~User();

    template<typename T> T* use_get(std::size_t i) const {
      PSI_STATIC_ASSERT_MSG((std::tr1::is_base_of<Used, T>::value), "T must inherit Psi::Used");
      return checked_pointer_static_cast<T>(use_n(i).target());
    }

    void use_set(std::size_t i, Used *target) {
      use_n(i).set_target(target);
    }

    std::size_t use_slots() const {
      return m_uses[0].n_uses();
    }
  };
}

#endif
