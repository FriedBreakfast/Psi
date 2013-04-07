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
PSI_COMPILER_COMMON_EXPORT String address_to_symbol(void *addr, void **base);

class PSI_COMPILER_COMMON_EXPORT PlatformError : public std::exception {
  std::string m_message;
public:
  PlatformError(const char*);
  PlatformError(const std::string&);
  virtual ~PlatformError() throw();
  virtual const char *what() const throw();
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
class PSI_COMPILER_COMMON_EXPORT PlatformLibrary {
public:
  virtual ~PlatformLibrary();
  virtual boost::optional<void*> symbol(const std::string& name) = 0;
};

/**
 * Generic library loading function.
 * 
 * Implementation is platform-specific.
 */
PSI_COMPILER_COMMON_EXPORT boost::shared_ptr<PlatformLibrary> load_library(const std::string& path);

class PSI_COMPILER_COMMON_EXPORT TemporaryPathImpl {
  std::string m_path;
  
public:
  TemporaryPathImpl(const std::string& path);
  virtual ~TemporaryPathImpl();
  virtual void delete_() = 0;
  const std::string& path() const {return m_path;}
};

/// Platform specific implemenation
TemporaryPathImpl* make_temporary_path_impl();

/**
 * \brief Temporary path helper class.
 * 
 * This class performs two functions: on construction, it gets an absolute
 * path not corresponding to an existing file.
 * When delete_() is called, or on class destruction, it deletes the file
 * at that path (if one exists).
 * This does not create the file.
 */
class PSI_COMPILER_COMMON_EXPORT TemporaryPath : public NonCopyable {
  TemporaryPathImpl *m_impl;
  
public:
  TemporaryPath();
  ~TemporaryPath();
  void delete_();
  const std::string& path() const {return m_impl->path();}
};

/**
 * \brief Join two paths to form a combined path.
 * 
 * If second is an absolute path, return second. Otherwise,
 * return second appended to first, with a separating slash
 * if first does not end with a slash already.
 */
PSI_COMPILER_COMMON_EXPORT std::string join_path(const std::string& first, const std::string& second);

/**
 * \brief Normalize a path.
 * 
 * Removes any occurences of './', '../' and '//'.
 * The resulting path will have a slash if the original path had one,
 * or the original path ended in a '..' or '.'.
 */
PSI_COMPILER_COMMON_EXPORT std::string normalize_path(const std::string& path);

/**
 * \brief Convert a relative path to an absolute path.
 * 
 * If this is already an absolute path, this is a no-op.
 */
PSI_COMPILER_COMMON_EXPORT std::string absolute_path(const std::string& path);

/**
 * \brief Get the filename portion of a path.
 */
PSI_COMPILER_COMMON_EXPORT std::string filename(const std::string& path);

/**
 * \brief Find an executable in the current path.
 */
PSI_COMPILER_COMMON_EXPORT boost::optional<std::string> find_in_path(const std::string& name);

/**
 * Run a command and send data to its standard in, capture standard out and return the result.
 * 
 * \param command Command line to execute
 * \param input Data to be passed to stdin
 * \param output_out stdout data
 * \param output_err stderr data
 */
PSI_COMPILER_COMMON_EXPORT int exec_communicate(const std::vector<std::string>& command, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);

PSI_COMPILER_COMMON_EXPORT void exec_communicate_check(const std::vector<std::string>& command, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);
PSI_COMPILER_COMMON_EXPORT void exec_communicate_check(const std::string& command, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);
}
}

#endif