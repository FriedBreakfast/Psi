#include "SourceLocation.hpp"

#include <sstream>
#if PSI_DEBUG
#include <iostream>
#endif

namespace Psi {
  LogicalSourceLocation::LogicalSourceLocation(const String& name, const LogicalSourceLocation *parent)
  : m_name(name), m_parent(parent) {
  }

  LogicalSourceLocation::~LogicalSourceLocation() {
  }

  /**
    * \brief Create a location with no parent. This should only be used by CompileContext.
    */
  LogicalSourceLocationPtr LogicalSourceLocation::new_root() {
    return LogicalSourceLocationPtr(new LogicalSourceLocation("", NULL));
  }

  /**
    * \brief Create a new named child of this location.
    */
  LogicalSourceLocationPtr LogicalSourceLocation::new_child(const String& name) const {
    return LogicalSourceLocationPtr(new LogicalSourceLocation(name, this));
  }

  /**
   * \brief Count the number of parent nodes between this location and the root node.
   */
  unsigned LogicalSourceLocation::depth() const {
    unsigned d = 0;
    for (const LogicalSourceLocation *l = this->parent().get(); l; l = l->parent().get())
      ++d;
    return d;
  } 

  /**
    * \brief Get the ancestor of this location which is a certain
    * number of parent nodes away.
    */
  LogicalSourceLocationPtr LogicalSourceLocation::ancestor(unsigned depth) const {
    const LogicalSourceLocation *ptr = this;
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
  String LogicalSourceLocation::error_name(const LogicalSourceLocationPtr& relative_to, bool null_root) const {
    unsigned print_depth = depth();
    if (relative_to) {
      // Find the common ancestor of this and relative_to.
      unsigned this_depth = print_depth;
      unsigned relative_to_depth = relative_to->depth();
      unsigned min_depth = std::min(this_depth, relative_to_depth);
      print_depth = this_depth - min_depth;
      const LogicalSourceLocation *this_ancestor = ancestor(print_depth).get();
      const LogicalSourceLocation *relative_to_ancestor = relative_to->ancestor(relative_to_depth - min_depth).get();

      while (this_ancestor != relative_to_ancestor) {
        ++print_depth;
        this_ancestor = this_ancestor->parent().get();
        relative_to_ancestor = relative_to_ancestor->parent().get();
      }
    }

    print_depth = std::max(print_depth, 1u);

    std::vector<const LogicalSourceLocation*> nodes;
    for (const LogicalSourceLocation *l = this; print_depth; l = l->parent().get(), --print_depth)
      nodes.push_back(l);

    if (!nodes.back()->parent()) {
      nodes.pop_back();
      if (nodes.empty()) {
        if (null_root)
          return "";
        else
          return "(root namespace)";
      }
    }

    std::stringstream ss;
    bool first = true;
    for (std::vector<const LogicalSourceLocation*>::reverse_iterator ii = nodes.rbegin(), ie = nodes.rend(); ii != ie; ++ii) {
      if (!first)
        ss << '.';
      else
        first = false;
      
      ss << (*ii)->name();
    }

    const std::string& sss = ss.str();
    return String(sss.c_str(), sss.length());
  }

#if PSI_DEBUG || PSI_DOXYGEN
  /**
    * \brief Dump the name of this location to stderr.
    *
    * Only available if \c PSI_DEBUG is not zero.
    */
  void LogicalSourceLocation::dump_error_name() const {
    std::cerr << error_name(LogicalSourceLocationPtr()) << std::endl;
  }
#endif

  SourceLocation SourceLocation::root_location(const String& url) {
    PhysicalSourceLocation phys;
    phys.file.reset(new SourceFile());
    phys.file->url = url;
    phys.first_line = phys.first_column = 1;
    phys.last_line = phys.last_column = 0;
    return SourceLocation(phys, LogicalSourceLocation::new_root());
  }
}
