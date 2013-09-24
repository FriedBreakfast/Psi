#ifndef HPP_PSI_PLATFORM
#define HPP_PSI_PLATFORM

#include <boost/optional.hpp>
#include <iosfwd>

#include "Runtime.hpp"
#include "PropertyValue.hpp"

#if defined(_WIN32)
#include "PlatformImplWindows.hpp"
#elif defined(__unix__)
#include "PlatformImplUnix.hpp"
#else
#error Unsupported platform
#endif

namespace Psi {
namespace Platform {
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

class PSI_COMPILER_COMMON_EXPORT Path {
  PathData m_data;

public:
  Path();
  Path(const PathData& data);
  Path(const char *pth);
  Path(const std::string& pth);
  ~Path();

  /// \brief Get the underlying representation of the path
  const PathData& data() const {return m_data;}

  /// \brief Convert this path to a string representation
  std::string str() const;

  /**
   * \brief Join two paths to form a combined path.
   * 
   * If second is an absolute path, return second. Otherwise,
   * return second appended to first, with a separating slash
   * if first does not end with a slash already.
   */
  Path join(const Path& second) const;

  /**
   * \brief Normalize a path.
   * 
   * Removes any occurences of './', '../' and '//'.
   * The resulting path will have a slash if the original path had one,
   * or the original path ended in a '..' or '.'.
   */
  Path normalize() const;

  /**
   * \brief Convert a relative path to an absolute path.
   * 
   * If this is already an absolute path, this is a no-op.
   */
  Path absolute() const;

  /**
   * \brief Get the filename portion of a path.
   */
  Path filename() const;
};

/// \brief Print a path to an output stream
PSI_COMPILER_COMMON_EXPORT std::ostream& operator << (std::ostream& os, const Path& pth);

/// \brief Get the current working directory
PSI_COMPILER_COMMON_EXPORT Path getcwd();

/**
  * \brief Find an executable in the current path.
  */
PSI_COMPILER_COMMON_EXPORT boost::optional<Path> find_in_path(const Path& name);

/**
 * Generic library loading function.
 * 
 * Implementation is platform-specific.
 */
PSI_COMPILER_COMMON_EXPORT boost::shared_ptr<PlatformLibrary> load_library(const Path& path);

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
  TemporaryPathData m_data;
  Path m_path;
  
public:
  TemporaryPath();
  ~TemporaryPath();
  void delete_();
  const Path& path() const {return m_path;}
};

/**
 * Run a command and send data to its standard in, capture standard out and return the result.
 * 
 * \param command Command to execute
 * \param args Arguments to pass to \c command
 * \param input Data to be passed to stdin
 * \param output_out stdout data
 * \param output_err stderr data
 */
PSI_COMPILER_COMMON_EXPORT int exec_communicate(const Path& command, const std::vector<std::string>& args,
                                                const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);

PSI_COMPILER_COMMON_EXPORT void exec_communicate_check(const Path& command, const std::vector<std::string>& args, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);
PSI_COMPILER_COMMON_EXPORT void exec_communicate_check(const Path& command, const std::string& input="", std::string *output_out=NULL, std::string *output_err=NULL);

/**
 * Read configuration data from files and update a configuration map.
 */
PSI_COMPILER_COMMON_EXPORT void read_configuration_files(PropertyValue& pv, const std::string& name);
}
}

#endif
