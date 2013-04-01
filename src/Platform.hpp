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
 * A platform library.
 * 
 * The only specified operations are that the symbol member allows access
 * to symbols and that destroying this object will free resources
 * associated with the loaded module.
 * 
 * How to construct this object is platform-specific, although see load_module().
 */
class PlatformLibrary {
public:
  virtual ~PlatformLibrary();
  virtual boost::optional<void*> symbol(const std::string& name) = 0;
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

/**
 * Run a command and send data to its standard in, capture standard out and return the result.
 * 
 * \param command Command line to execute
 * \param input Data to be passed to stdin
 * \param output_out stdout data
 * \param output_err stderr data
 */
int exec_communicate(const std::vector<std::string>& command, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);

void exec_communicate_check(const std::vector<std::string>& command, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);
}
}

#endif