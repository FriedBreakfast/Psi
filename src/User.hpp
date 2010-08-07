#ifndef HPP_PSI_USER
#define HPP_PSI_USER

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Utility.hpp"

/**
 * \file
 *
 * A similar idea to LLVMs User/Use class.
 */

namespace Psi {
  class User;
  class Used;

  class Use {
    Use(const Use&) = delete;

  public:
    enum UseMode {
      UserHeadInline = 1,
      UserHeadMalloc = 2,
      UsedHead = 3
    };

    Use() = default;

    void init_user_head(bool is_inline, User *owner, std::size_t n_uses) {
      m_target = is_inline ? UserHeadInline : UserHeadMalloc;
      m_rest.head.owner = owner;
      m_rest.head.n_uses = n_uses;
    }

    void init_use_node() {
      m_target = 0;
      m_rest.use.next = NULL;
      m_rest.use.prev = NULL;
    }

    void init_used_head() {
      m_target = UsedHead;
      m_rest.use.next = this;
      m_rest.use.prev = this;
    }

    bool user_head() {return (m_target == UserHeadInline) || (m_target == UserHeadMalloc);}
    bool used_head() {return m_target == UsedHead;}
    bool use_node() {return (m_target == 0) || (m_target > UsedHead);}

    User *owner() {PSI_ASSERT(user_head(), "wrong Use type"); return m_rest.head.owner;}
    std::size_t n_uses() {PSI_ASSERT(user_head(), "wrong Use type"); return m_rest.head.n_uses;}

    Use *locate_head() {
      PSI_ASSERT(use_node(), "wrong Use type");

      Use *u = this;
      while (true) {
	--u;
	if (u->user_head())
	  return u;
      }
    }

    Use *next() {PSI_ASSERT(!user_head(), "wrong Use type"); return m_rest.use.next;}
    Use *prev() {PSI_ASSERT(!user_head(), "wrong Use type"); return m_rest.use.prev;}
    Used *target() {PSI_ASSERT(use_node(), "wrong Use type"); return reinterpret_cast<Used*>(m_target);}

    // "use_node" operations
    void set_target(Used *target);

    // "used_head" operations
    void clear_users();
    void replace_with(Used *target);

  private:
    std::intptr_t m_target;

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

  class Used : public CheckedCastBase {
    friend class Use;

    Used(const Used&) = delete;

  private:
    Use m_use;

  protected:
    Used();

    void replace_with(Used *target) {m_use.replace_with(target);}
  };

  template<int N>
  class StaticUses {
    friend class User;
  private:
    Use uses[N+1];
  };

  class User {
  private:
    User(const User&) = delete;

    Use *m_uses;

    Use& use_n(std::size_t n) {
      PSI_ASSERT(m_uses && (n < m_uses[0].n_uses()), "Use index out of range");
      return m_uses[n+1];
    }

  protected:
    User() : m_uses(0) {}

    template<int N>
    void init_uses(StaticUses<N>& st) {
      m_uses = st.uses;
      m_uses[0].init_user_head(true, this, N);
      for (int i = 0; i < N; ++i)
	m_uses[i+1].init_use_node();
    }

    virtual ~User();

    template<typename T> T* use_get(std::size_t i) {
      PSI_STATIC_ASSERT((std::is_base_of<Used, T>::value), "T must inherit Psi::Used");
      return checked_pointer_static_cast<T>(use_n(i).target());
    }

    void use_set(std::size_t i, Used *target) {
      use_n(i).set_target(target);
    }

    std::size_t use_slots() {
      return m_uses[0].n_uses();
    }
  };

  class UsePtr : public User {
  private:
    StaticUses<2> m_uses;
    void init() {init_uses(m_uses);}

  public:
    UsePtr() {init();}
    UsePtr(Used *u) {init(); set(u);}
    ~UsePtr() {}

    template<typename T> T* get() {return use_get<T>(1);}
    void set(Used *u) {use_set(1, u);}
  };
}

#endif
