#include "SourceLocation.hpp"

namespace Psi {
  bool LogicalSourceLocation::Key::operator < (const Key& other) const {
    if (index) {
      if (other.index)
        return index < other.index;
      else
        return false;
    } else {
      if (other.index)
        return true;
      else
        return name < other.name;
    }
  } 

  bool LogicalSourceLocation::Compare::operator () (const LogicalSourceLocation& lhs, const LogicalSourceLocation& rhs) const {
    return lhs.m_key < rhs.m_key;
  }

  struct LogicalSourceLocation::KeyCompare {
    bool operator () (const Key& key, const LogicalSourceLocation& node) const {
      return key < node.m_key;
    }

    bool operator () (const LogicalSourceLocation& node, const Key& key) const {
      return node.m_key < key;
    }
  };

  LogicalSourceLocation::LogicalSourceLocation(const Key& key, const LogicalSourceLocationPtr& parent)
    : m_reference_count(0), m_key(key), m_parent(parent) {
  }

  LogicalSourceLocation::~LogicalSourceLocation() {
    if (m_parent)
      m_parent->m_children.erase(m_parent->m_children.iterator_to(*this));
  }

  /**
    * \brief Create a location with no parent. This should only be used by CompileContext.
    */
  LogicalSourceLocationPtr LogicalSourceLocation::new_root_location() {
    Key key;
    key.index = 0;
    return LogicalSourceLocationPtr(new LogicalSourceLocation(key, LogicalSourceLocationPtr()));
  }

  /**
    * \brief Create a new named child of this location.
    */
  LogicalSourceLocationPtr LogicalSourceLocation::named_child(const String& name) {
    Key key;
    key.index = 0;
    key.name = name;
    ChildMapType::insert_commit_data commit_data;
    std::pair<ChildMapType::iterator, bool> result = m_children.insert_check(key, KeyCompare(), commit_data);

    if (!result.second)
      return LogicalSourceLocationPtr(&*result.first);

    LogicalSourceLocationPtr node(new LogicalSourceLocation(key, LogicalSourceLocationPtr(this)));
    m_children.insert_commit(*node, commit_data);
    return node;
  }

  LogicalSourceLocationPtr LogicalSourceLocation::new_anonymous_child() {
    unsigned index = 1;
    ChildMapType::iterator end = m_children.end();
    if (!m_children.empty()) {
            ChildMapType::iterator last = end;
            --last;
            if (last->anonymous())
              index = last->index() + 1;
    }

    Key key;
    key.index = index;
    LogicalSourceLocationPtr node(new LogicalSourceLocation(key, LogicalSourceLocationPtr(this)));
    m_children.insert(end, *node);
    return node;
  }

  /**
    * \brief Count the number of parent nodes between this location and the root node.
    */
  unsigned LogicalSourceLocation::depth() {
    unsigned d = 0;
    for (LogicalSourceLocation *l = this->parent().get(); l; l = l->parent().get())
      ++d;
    return d;
  } 

  /**
    * \brief Get the ancestor of this location which is a certain
    * number of parent nodes away.
    */
  LogicalSourceLocationPtr LogicalSourceLocation::ancestor(unsigned depth) {
    LogicalSourceLocation *ptr = this;
    for (unsigned i = 0; i != depth; ++i)
      ptr = ptr->parent().get();
    return LogicalSourceLocationPtr(ptr);
  }

  /**
    * \brief Get the full name of this location for use in an error message.
    *
    * \param relative_to Location at which the error occurred, so
    * that a common prefix may be skipped.
    *
    * \param ignore_anonymous_tail Do not include anonymous nodes at
    * the bottom of the tree.
    */
  String LogicalSourceLocation::error_name(const LogicalSourceLocationPtr& relative_to, bool ignore_anonymous_tail) {
    unsigned print_depth = depth();
    if (relative_to) {
            // Find the common ancestor of this and relative_to.
            unsigned this_depth = print_depth;
            unsigned relative_to_depth = relative_to->depth();
            unsigned min_depth = std::min(this_depth, relative_to_depth);
            print_depth = this_depth - min_depth;
            LogicalSourceLocation *this_ancestor = ancestor(print_depth).get();
            LogicalSourceLocation *relative_to_ancestor = relative_to->ancestor(relative_to_depth - min_depth).get();

            while (this_ancestor != relative_to_ancestor) {
              ++print_depth;
              this_ancestor = this_ancestor->parent().get();
              relative_to_ancestor = relative_to_ancestor->parent().get();
            }
    }

    print_depth = std::max(print_depth, 1u);

    std::vector<LogicalSourceLocation*> nodes;
    bool last_anonymous = false;
    for (LogicalSourceLocation *l = this; print_depth; l = l->parent().get(), --print_depth) {
      if (!l->anonymous()) {
              nodes.push_back(l);
              last_anonymous = false;
      } else {
              if (!last_anonymous)
                nodes.push_back(l);
              last_anonymous = true;
      }
    }

    if (ignore_anonymous_tail) {
            if (nodes.front()->anonymous())
              nodes.erase(nodes.begin());
            if (nodes.empty())
              return "(anonymous)";
    }

    if (!nodes.back()->parent()) {
            nodes.pop_back();
            if (nodes.empty())
              return "(root namespace)";
    }

    std::stringstream ss;
    for (std::vector<LogicalSourceLocation*>::reverse_iterator ib = nodes.rbegin(),
          ii = nodes.rbegin(), ie = nodes.rend(); ii != ie; ++ii) {
            if (ii != ib)
              ss << '.';

            if ((*ii)->anonymous())
              ss << "(anonymous)";
            else
              ss << (*ii)->name();
    }

    const std::string& sss = ss.str();
    return String(sss.c_str(), sss.length());
  }

#if defined(PSI_DEBUG) || defined(PSI_DOXYGEN)
  /**
    * \brief Dump the name of this location to stderr.
    *
    * Only available if \c PSI_DEBUG is defined.
    */
  void LogicalSourceLocation::dump_error_name() {
    std::cerr << error_name(LogicalSourceLocationPtr()) << std::endl;
  }
#endif
}
