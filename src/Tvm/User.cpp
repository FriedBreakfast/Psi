#include "User.hpp"

namespace Psi {
  void Use::init_user_head(bool is_inline, User *owner, std::size_t n_uses) {
    m_target = is_inline ? UserHeadInline : UserHeadMalloc;
    m_rest.head.owner = owner;
    m_rest.head.n_uses = n_uses;
  }

  void Use::init_use_node() {
    m_target = 0;
    m_rest.use.next = NULL;
    m_rest.use.prev = NULL;
  }

  void Use::init_used_head() {
    m_target = UsedHead;
    m_rest.use.next = this;
    m_rest.use.prev = this;
  }

  void Use::set_target(Used *target) {
    // Require the correct Use type
    PSI_ASSERT(use_node(), "wrong Use type");

    if (m_target) {
      m_rest.use.next->m_rest.use.prev = m_rest.use.prev;
      m_rest.use.prev->m_rest.use.next = m_rest.use.next;
    }

    if (target) {
      m_target = reinterpret_cast<std::tr1::intptr_t>(target);

      Use *prev = &target->m_use;
      Use *next = prev->m_rest.use.next;
      prev->m_rest.use.next = this;
      next->m_rest.use.prev = this;
      m_rest.use.next = next;
      m_rest.use.prev = prev;
    } else {
      m_target = 0;
      m_rest.use.next = NULL;
      m_rest.use.prev = NULL;
    }
  }

  void Use::clear_users() {
    // Require the correct Use type
    PSI_ASSERT(used_head(), "wrong Use type");

    for (Use *u = m_rest.use.next, *next_u = u->m_rest.use.next;
	 u != this;
	 u = next_u, next_u = u->m_rest.use.next) {
      u->m_target = 0;
      u->m_rest.use.next = NULL;
      u->m_rest.use.prev = NULL;
    }

    m_rest.use.next = this;
    m_rest.use.prev = this;
  }

  void Use::replace_with(Used *target) {
    // Require the correct Use type
    PSI_ASSERT(used_head(), "wrong Use type");

    for (Use *u = m_rest.use.next; u != this; u = u->m_rest.use.next)
      u->m_target = reinterpret_cast<std::tr1::intptr_t>(target);

    Use *prev = &target->m_use;
    Use *next = prev->m_rest.use.next;
    m_rest.use.next->m_rest.use.prev = prev;
    m_rest.use.prev->m_rest.use.next = next;
    prev->m_rest.use.next = m_rest.use.next;
    next->m_rest.use.prev = m_rest.use.prev;

    m_rest.use.next = this;
    m_rest.use.prev = this;
  }

  Used::Used() {
    m_use.init_used_head();
  }

  Used::~Used() {
    m_use.clear_users();
  }

  User::User(const UserInitializer& ui) {
    m_uses = ui.m_uses;
    m_uses[0].init_user_head(true, this, ui.m_n_uses);
    for (std::size_t i = 0; i < ui.m_n_uses; ++i)
      m_uses[i+1].init_use_node();
  }

  User::~User() {
    std::size_t n = m_uses[0].n_uses();
    for (std::size_t i = 0; i < n; ++i)
      m_uses[i+1].set_target(NULL);
  }
}