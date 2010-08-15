#ifndef HPP_PSI_USER
#define HPP_PSI_USER

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

  class Used : public CheckedCastBase {
    friend class Use;

  private:
    Used(const Used&);
    Use m_use;

  protected:
    Used();
    ~Used();

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
      PSI_ASSERT(m_uses && (n < m_uses[0].n_uses()), "Use index out of range");
      return m_uses[n+1];
    }

  protected:
    User(const UserInitializer& ui);
    virtual ~User();

    template<typename T> T* use_get(std::size_t i) const {
      PSI_STATIC_ASSERT((std::tr1::is_base_of<Used, T>::value), "T must inherit Psi::Used");
      return checked_pointer_static_cast<T>(use_n(i).target());
    }

    void use_set(std::size_t i, Used *target) {
      use_n(i).set_target(target);
    }

    std::size_t use_slots() {
      return m_uses[0].n_uses();
    }
  };
}

#endif
