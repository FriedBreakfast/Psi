#include "Array.hpp"
#include "PlatformLinux.hpp"

#include <boost/format.hpp>

#include <errno.h>
#include <string.h>
#include <unistd.h>

namespace Psi {
namespace Platform {
namespace Linux {
/**
 * Translate an error number into a string.
 */
std::string error_string(int errcode) {
  const std::size_t buf_size = 64;
  SmallArray<char, buf_size> data(buf_size);
  while (true) {
    char *ptr = strerror_r(errcode, data.get(), data.size());
    if (ptr != data.get())
      return ptr;
    if (strlen(data.get())+1 < data.size())
      return data.get();

    data.resize(data.size() * 2);
  }
}

std::string getcwd() {
  const std::size_t path_max = 256;
  SmallArray<char, path_max> data;
  data.resize(path_max);
  while (true) {
    if (::getcwd(data.get(), data.size()))
      return data.get();
    
    int errcode = errno;
    if (errcode == ERANGE)
      data.resize(data.size() * 2);
    else
      throw PlatformError(boost::str(boost::format("Could not get working directory: %s") % Platform::Linux::error_string(errcode)));
  }
}
}

/**
 * \brief Look for an executable in the path.
 * 
 * If \c name contains no slashes, search the path for an executable file with the given name.
 * Otherwise translate \c name to an absolute path.
 */
boost::optional<std::string> find_in_path(const std::string& name) {
  std::string found_name;
  
  if (!name.find('/')) {
    // Relative or absolute path
    if (access(name.c_str(), X_OK) == 0)
      found_name = name;
    else
      return boost::none;
  } else {
    // Search the system path
    const char *path = std::getenv("PATH");
    if (!path)
      return boost::none;
    
    std::string found_name;
    
    while (true) {
      const char *end = std::strchr(path, ':');
      if (!end)
        end = path + std::strlen(path);
      found_name.assign(path, end);
      if (found_name.empty()) {
        // Means current directory
        found_name = name;
      } else {
        if (found_name.at(found_name.length()-1) != '/')
          found_name.push_back('/');
        found_name.append(name);
      }
      
      if (access(found_name.c_str(), X_OK) == 0) {
        break;
      }
      
      if (*end == '\0')
        return boost::none;
    }
  }
  
  // Convert to absolute path

  return absolute_path(found_name);
}

std::string join_path(const std::string& first, const std::string& second) {
  if (first.empty())
    return second;
  else if (second.empty())
    return first;
  
  if (second.at(0) == '/')
    return second;
  
  if (first.at(first.length()-1) == '/')
    return first + second;
  else
    return first + '/' + second;
}

std::string normalize_path(const std::string& path) {
  if (path.empty())
    return path;
  
  std::string result;
  std::string::size_type pos = 0;
  while (true) {
    std::string::size_type next_pos = path.find('/', pos);
    if (next_pos == pos) {
      result = '/';
    } else {
      std::string part = path.substr(pos, next_pos);
      if (part == ".") {
      } else if (part == "..") {
        if (result.empty()) {
          result = "../";
        } else {
          PSI_ASSERT(result.at(result.length()-1) == '/');
          std::string::size_type last_pos = result.rfind('/', result.length()-1);
          if (last_pos == std::string::npos) {
            result.clear();
          } else {
            ++last_pos;
            std::string::size_type count = result.length() - last_pos;
            if ((count == 3) && (result.substr(last_pos) == "../"))
              result += "../";
            else
              result.erase(last_pos);
          }
        }
      } else {
        result += part;
        if (next_pos != std::string::npos)
          result += '/';
      }
    }
    
    if (next_pos == std::string::npos)
      break;
    
    pos = next_pos + 1;
    if (pos == path.length())
      break;
  }
  
  return result;
}

std::string absolute_path(const std::string& path) {
  if (path.empty())
    throw PlatformError("Cannot convert empty path to absolute path");
  
  if (path.at(0) == '/')
    return path;
  
  return normalize_path(join_path(Linux::getcwd(), path));
}

std::string filename(const std::string& path) {
  std::string::size_type n = path.rfind('/');
  if (n == std::string::npos)
    return path;
  else
    return path.substr(n+1);
}
}
}
