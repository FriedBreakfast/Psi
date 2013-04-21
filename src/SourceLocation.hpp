#ifndef HPP_PSI_SOURCE_LOCATION
#define HPP_PSI_SOURCE_LOCATION

#include "Runtime.hpp"
#include "Visitor.hpp"
#include "Export.hpp"

#include <boost/intrusive/avl_set.hpp>

namespace Psi {
  struct SourceFile {
    String url;
  };

  struct PhysicalSourceLocation {
    SharedPtr<SourceFile> file;
    int first_line;
    int first_column;
    int last_line;
    int last_column;
  };

  class LogicalSourceLocation;
  typedef IntrusivePointer<LogicalSourceLocation> LogicalSourceLocationPtr;

  class LogicalSourceLocationBase : public boost::intrusive::avl_set_base_hook<> {};

  class LogicalSourceLocation : public LogicalSourceLocationBase {
    struct Key {
      unsigned index;
      String name;

      bool operator < (const Key&) const;
    };

    struct Compare {bool operator () (const LogicalSourceLocation&, const LogicalSourceLocation&) const;};
    struct KeyCompare;

    std::size_t m_reference_count;
    Key m_key;
    LogicalSourceLocationPtr m_parent;
    typedef boost::intrusive::avl_set<LogicalSourceLocation, boost::intrusive::constant_time_size<false>, boost::intrusive::compare<Compare> > ChildMapType;
    ChildMapType m_children;

    LogicalSourceLocation(const Key&, const LogicalSourceLocationPtr&);

  public:
    PSI_COMPILER_COMMON_EXPORT ~LogicalSourceLocation();

    /// \brief Whether this location is anonymous within its parent.
    bool anonymous() {return m_parent && (m_key.index != 0);}
    /// \brief The identifying index of this location if it is anonymous.
    unsigned index() {return m_key.index;}
    /// \brief The name of this location within its parent if it is not anonymous.
    const String& name() {return m_key.name;}
    /// \brief Get the parent node of this location
    const LogicalSourceLocationPtr& parent() {return m_parent;}

    PSI_COMPILER_COMMON_EXPORT unsigned depth();
    PSI_COMPILER_COMMON_EXPORT LogicalSourceLocationPtr ancestor(unsigned depth);
    PSI_COMPILER_COMMON_EXPORT String error_name(const LogicalSourceLocationPtr& relative_to, bool ignore_anonymous=false, bool null_root=false);
#if PSI_DEBUG || PSI_DOXYGEN
    PSI_COMPILER_COMMON_EXPORT void dump_error_name();
#endif

    PSI_COMPILER_COMMON_EXPORT static LogicalSourceLocationPtr new_root_location();
    PSI_COMPILER_COMMON_EXPORT LogicalSourceLocationPtr named_child(const String& name);
    PSI_COMPILER_COMMON_EXPORT LogicalSourceLocationPtr new_anonymous_child();

    friend void intrusive_ptr_add_ref(LogicalSourceLocation *self) {
      ++self->m_reference_count;
    }

    friend void intrusive_ptr_release(LogicalSourceLocation *self) {
      if (!--self->m_reference_count)
        delete self;
    }
  };

  struct SourceLocation {
    PhysicalSourceLocation physical;
    LogicalSourceLocationPtr logical;
    
    SourceLocation() {}

    SourceLocation(const PhysicalSourceLocation& physical_,  const LogicalSourceLocationPtr& logical_)
    : physical(physical_), logical(logical_) {}

    SourceLocation relocate(const PhysicalSourceLocation& new_physical) const {
      return SourceLocation(new_physical, logical);
    }

    SourceLocation named_child(const String& name) const {
      return SourceLocation(physical, logical->named_child(name));
    }
    
    PSI_COMPILER_COMMON_EXPORT static SourceLocation root_location(const String& name);
  };
  
  PSI_VISIT_SIMPLE(SourceLocation);
}

#endif