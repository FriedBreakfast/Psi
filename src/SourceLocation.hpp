#ifndef HPP_PSI_SOURCE_LOCATION
#define HPP_PSI_SOURCE_LOCATION

#include "Runtime.hpp"
#include "Visitor.hpp"
#include "Export.hpp"

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
  typedef IntrusivePointer<const LogicalSourceLocation> LogicalSourceLocationPtr;

  class PSI_COMPILER_COMMON_EXPORT LogicalSourceLocation {
    mutable ReferenceCount m_reference_count;
    String m_name;
    LogicalSourceLocationPtr m_parent;

    LogicalSourceLocation(const String& name, const LogicalSourceLocation *parent);

  public:
    ~LogicalSourceLocation();

    /// \brief The name of this location within its parent if it is not anonymous.
    const String& name() const {return m_name;}
    /// \brief Get the parent node of this location
    const LogicalSourceLocationPtr& parent() const {return m_parent;}

    unsigned depth() const;
    LogicalSourceLocationPtr ancestor(unsigned depth) const;
    String error_name(const LogicalSourceLocationPtr& relative_to, bool null_root=false) const;
#if PSI_DEBUG || PSI_DOXYGEN
    void dump_error_name() const;
#endif

    static LogicalSourceLocationPtr new_root();
    LogicalSourceLocationPtr new_child(const String& name) const;

    friend void intrusive_ptr_add_ref(const LogicalSourceLocation *self) {
      self->m_reference_count.acquire();
    }
    
    friend void intrusive_ptr_release(const LogicalSourceLocation *self) {
      if (self->m_reference_count.release())
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
      return SourceLocation(physical, logical->new_child(name));
    }
    
    PSI_COMPILER_COMMON_EXPORT static SourceLocation root_location(const String& name);
  };
  
  PSI_VISIT_SIMPLE(SourceLocation);
}

#endif
