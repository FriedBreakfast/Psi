#ifndef HPP_PSI_PLATFORM
#define HPP_PSI_PLATFORM

#include <boost/optional.hpp>

#include "Runtime.hpp"

namespace Psi {
namespace Platform {
/**
 * \brief Convert the address of a function or global into a symbol name.
 * 
 * \param base If non-NULL, the actual base address of the symbol
 * is stored here.
 */
String address_to_symbol(void *addr, void **base);

class PlatformError : public std::runtime_error {
public:
  PlatformError(const char*);
  PlatformError(const std::string&);
  virtual ~PlatformError() throw();
};

/**
 * \brief Join two paths to form a combined path.
 * 
 * If second is an absolute path, return second. Otherwise,
 * return second appended to first, with a separating slash
 * if first does not end with a slash already.
 */
std::string join_path(const std::string& first, const std::string& second);

/**
 * \brief Normalize a path.
 * 
 * Removes any occurences of './', '../' and '//'.
 * The resulting path will have a slash if the original path had one,
 * or the original path ended in a '..' or '.'.
 */
std::string normalize_path(const std::string& path);

/**
 * \brief Convert a relative path to an absolute path.
 * 
 * If this is already an absolute path, this is a no-op.
 */
std::string absolute_path(const std::string& path);

/**
 * \brief Get the filename portion of a path.
 */
std::string filename(const std::string& path);

/**
 * \brief Find an executable in the current path.
 */
boost::optional<std::string> find_in_path(const std::string& name);
}
}

#endif