#include "Array.hpp"
#include "PlatformUnix.hpp"

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <fstream>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

namespace Psi {
namespace Platform {
namespace Unix {
/// Template class which owns a pointer allocated by malloc()
template<typename T>
class MallocPtr : public NonCopyable, public PointerBase<T> {
public:
  MallocPtr(T *ptr=NULL) : PointerBase<T>(ptr) {}
  ~MallocPtr() {reset();}

  void reset(T *ptr=NULL) {
    if (PointerBase<T>::m_ptr)
      free(PointerBase<T>::m_ptr);
    PointerBase<T>::m_ptr = ptr;
  }
};

/**
 * \param hint Number of entries in the handle vector to reserve
 */
LibraryUnix::LibraryUnix(unsigned hint) {
  m_handles.reserve(hint);
}

LibraryUnix::~LibraryUnix() {
  while (!m_handles.empty()) {
    dlclose(m_handles.back());
    m_handles.pop_back();
  }
}

boost::optional<void*> LibraryUnix::symbol(const std::string& symbol) {
  dlerror();
  for (std::vector<void*>::const_reverse_iterator ii = m_handles.rbegin(), ie = m_handles.rend(); ii != ie; ++ii) {
    void *ptr = dlsym(*ii, symbol.c_str());
    if (!dlerror())
      return ptr;
  }
  
  return boost::none;
}

/**
 * Take ownership of a handle, and add it to this library..
 */
void LibraryUnix::add_handle(void* handle) {
  try {
    m_handles.push_back(handle);
  } catch (...) {
    dlclose(handle);
    throw;
  }
}

/**
 * Translate an error number into a string.
 */
std::string error_string(int errcode) {
  const std::size_t buf_size = 64;
  SmallArray<char, buf_size> data(buf_size);
  while (true) {
    char *ptr = sys_strerror_r(errcode, data.get(), data.size());
    if (!ptr)
      return "Failed to retrieve error description";
    if (ptr != data.get())
      return ptr;
    if (strlen(data.get())+1 < data.size())
      return data.get();

    data.resize(data.size() * 2);
  }
}
}

/**
 * \brief Look for an executable in the path.
 * 
 * If \c name contains no slashes, search the path for an executable file with the given name.
 * Otherwise translate \c name to an absolute path.
 */
boost::optional<Path> find_in_path(const Path& name) {
  std::string found_name;
  
  const std::string& name_str = name.data().path;
  if (!name_str.find('/')) {
    // Relative or absolute path
    if (access(name_str.c_str(), X_OK) == 0)
      found_name = name_str;
    else
      return boost::none;
  } else {
    // Search the system path
    const char *path = std::getenv("PATH");
    if (!path)
      return boost::none;
    
    while (true) {
      const char *end = std::strchr(path, ':');
      if (!end)
        end = path + std::strlen(path);
      found_name.assign(path, end);
      if (found_name.empty()) {
        // Means current directory
        found_name = name_str;
      } else {
        if (found_name.at(found_name.length()-1) != '/')
          found_name.push_back('/');
        found_name.append(name_str);
      }
      
      if (access(found_name.c_str(), X_OK) == 0)
        break;
      
      if (*end == '\0')
        return boost::none;
      
      path = end + 1;
    }
  }
  
  // Convert to absolute path

  return Path(found_name).absolute();
}

Path::Path() {
}

Path::Path(const char *path)
: m_data(path) {
}

Path::Path(const std::string& path)
: m_data(path) {
}

Path::Path(const PathData& path)
: m_data(path) {
}

Path::~Path() {
}

std::string Path::str() const {
  return m_data.path;
}

Path Path::join(const Path& other) const {
  if (m_data.path.empty())
    return other;
  else if (other.m_data.path.empty())
    return *this;
  
  if (other.m_data.path.at(0) == '/')
    return other;
  
  if (m_data.path.at(m_data.path.length()-1) == '/')
    return Path(m_data.path + other.m_data.path);
  else
    return Path(m_data.path + '/' + other.m_data.path);
}

Path Path::normalize() const {
  if (m_data.path.empty())
    return *this;
  
  std::string result;
  std::string::size_type pos = 0;
  while (true) {
    std::string::size_type next_pos = m_data.path.find('/', pos);
    if (next_pos == pos) {
      result = '/';
    } else {
      std::string part = m_data.path.substr(pos, next_pos);
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
    if (pos == m_data.path.length())
      break;
  }
  
  return result;
}

Path Path::absolute() const {
  if (m_data.path.empty())
    throw PlatformError("Cannot convert empty path to absolute path");
  
  if (m_data.path.at(0) == '/')
    return *this;
  
  return getcwd().join(*this).normalize();
}

std::ostream& operator << (std::ostream& os, const Path& pth) {
  return os << pth.data().path;
}

Path getcwd() {
  const std::size_t path_max = 256;
  SmallArray<char, path_max> data;
  data.resize(path_max);
  while (true) {
    if (::getcwd(data.get(), data.size()))
      return Path(data.get());
    
    int errcode = errno;
    if (errcode == ERANGE)
      data.resize(data.size() * 2);
    else
      throw PlatformError(boost::str(boost::format("Could not get working directory: %s") % Platform::Unix::error_string(errcode)));
  }
}

Path Path::filename() const {
  std::string::size_type n = m_data.path.rfind('/');
  if (n == std::string::npos)
    return m_data.path;
  else
    return m_data.path.substr(n+1);
}

namespace {
/**
 * RAII class for Unix file descriptors.
 */
class FileDescriptor {
  int m_fd;
  
public:
  FileDescriptor() : m_fd(-1) {}
  
  ~FileDescriptor() {
    close();
  }
  
  bool is_open() const {return m_fd >= 0;}
  int fd() {return m_fd;}
  void set_fd(int fd) {close(); m_fd = fd;}
  
  void close() {
    if (is_open()) {
      ::close(m_fd);
      m_fd = -1;
    }
  }
};

void cmd_pipe(FileDescriptor& read, FileDescriptor& write) {
  int p[2];
  if (Unix::sys_pipe(p) != 0) {
    int errcode = errno;
    throw PlatformError(boost::str(boost::format("Failed to create pipe for interprocess communication: %s") % Unix::error_string(errcode)));
  }
  read.set_fd(p[0]);
  write.set_fd(p[1]);
}

void cmd_set_nonblock(int fd) {
  int err = fcntl(fd, F_SETFL, O_NONBLOCK);
  if (err < 0) {
    int errcode = errno;
    throw PlatformError(boost::str(boost::format("Failed to set up nonblocking I/O mode for interprocess communication: %s") % Unix::error_string(errcode)));
  }
}

/**
 * Read data from a file descriptor into a buffer until end-of-file, or EAGAIN is returned.
 * If end-of-file is reached, the file is closed.
 * 
 * \return True if more data is available.
 */
bool cmd_read_by_buffer(FileDescriptor& fd, std::vector<char>& buffer, std::vector<char>& output) {
  while (true) {
    int n = read(fd.fd(), vector_begin_ptr(buffer), buffer.size());
    if (n == 0) {
      return false;
    } else if (n < 0) {
      int errcode = errno;
      if (errcode == EAGAIN)
        return true;
      else
        throw PlatformError(boost::str(boost::format("Failed to read from pipe during interprocess communication: %s") % Unix::error_string(errcode)));
    } else {
      output.insert(output.end(), buffer.begin(), buffer.begin() + n);
    }
  }
}

bool cmd_write_by_buffer(FileDescriptor& fd, const char*& ptr, const char *end) {
  int n = write(fd.fd(), ptr, end-ptr);
  if (n < 0) {
    int errcode = errno;
    if (errcode == EAGAIN)
      return true;
    else
      throw PlatformError(boost::str(boost::format("Failed to write to pipe during interprocess communication: %s") % Unix::error_string(errcode)));
  } else {
    ptr += n;
    return ptr != end;
  }
}
}

int exec_communicate(const Path& command, const std::vector<std::string>& args, const std::string& input, std::string *output_out, std::string *output_err) {
  // Read/write direction refers to the parent process
  FileDescriptor stdin_read, stdin_write, stdout_read, stdout_write, stderr_read, stderr_write;
  cmd_pipe(stdin_read, stdin_write);
  cmd_pipe(stdout_read, stdout_write);
  cmd_pipe(stderr_read, stderr_write);
  
  CStringArray c_args(args.size()+2);
  c_args[0] = CStringArray::checked_strdup(command.str());
  for (std::size_t ii = 0, ie = args.size(); ii != ie; ++ii)
    c_args[ii+1] = CStringArray::checked_strdup(args[ii]);
  c_args[args.size()+1] = NULL;
  
  pid_t child_pid = Unix::sys_fork_exec(stdin_read.fd(), stdout_write.fd(), stderr_write.fd(), c_args.data());
  if (child_pid == -1) {
    int errcode = errno;
    throw PlatformError(boost::str(boost::format("Failed to spawn child process: %s") % Unix::error_string(errcode)));
  }
    
  
  // Parent process
  stdin_read.close();
  stdout_write.close();
  stderr_write.close();
  
  cmd_set_nonblock(stdin_write.fd());
  cmd_set_nonblock(stdout_read.fd());
  cmd_set_nonblock(stderr_read.fd());
  
  std::vector<char> buffer(1024);
  std::vector<char> stdout_data, stderr_data;
  std::vector<char> stdin_data(input.begin(), input.end());
  const char *write_ptr = vector_begin_ptr(stdin_data), *write_end = vector_end_ptr(stdin_data);

  class PollHandler {
    int n_poll_fds;
    struct pollfd poll_fds[3];
    
  public:
    PollHandler() : n_poll_fds(0) {}
    
    int operator [] (int offset) const {
      PSI_ASSERT((offset >= 0) && (offset < 3));
      return poll_fds[offset].revents;
    }

    int enqueue_if(FileDescriptor& fd, int events, bool keep_open) {
      PSI_ASSERT(n_poll_fds < 3);
      PSI_ASSERT(fd.is_open());
      
      if (keep_open) {
        int idx = n_poll_fds++;
        poll_fds[idx].fd = fd.fd();
        poll_fds[idx].events = events;
        return idx;
      } else {
        fd.close();
        return -1;
      }
    }
    
    bool empty() const {
      return n_poll_fds == 0;
    }
    
    int do_poll() {
      int err = poll(poll_fds, n_poll_fds, -1);
      n_poll_fds = 0;
      return err;
    }
  };
  
  PollHandler poll_handler;
  int stdin_idx = poll_handler.enqueue_if(stdin_write, POLLOUT, true);
  int stdout_idx = poll_handler.enqueue_if(stdout_read, POLLIN, true);
  int stderr_idx = poll_handler.enqueue_if(stderr_read, POLLIN, true);
  
  while (true) {
    if (poll_handler.empty())
      break;
    
    if (poll_handler.do_poll() < 0) {
      int errcode = errno;
      throw PlatformError(boost::str(boost::format("Failure during interprocess communication in poll(): %s") % Unix::error_string(errcode)));
    }
    
    if (stdin_write.is_open())
      stdin_idx = poll_handler.enqueue_if(stdin_write, POLLOUT, poll_handler[stdin_idx] ? cmd_write_by_buffer(stdin_write, write_ptr, write_end) : true);
    
    if (stdout_read.is_open())
      stdout_idx = poll_handler.enqueue_if(stdout_read, POLLIN, poll_handler[stdout_idx] ? cmd_read_by_buffer(stdout_read, buffer, stdout_data) : true);
    
    if (stderr_read.is_open())
      stderr_idx = poll_handler.enqueue_if(stderr_read, POLLIN, poll_handler[stderr_idx] ? cmd_read_by_buffer(stderr_read, buffer, stderr_data) : true);
  }
  
  int child_status;
  if (waitpid(child_pid, &child_status, 0) == -1) {
    int errcode = errno;
    throw PlatformError(boost::str(boost::format("Could not get child process exit status: %s") % Unix::error_string(errcode)));
  }
  
  if (WIFEXITED(child_status)) {
    child_status = WEXITSTATUS(child_status);
  } else {
    // Unknown failure status
    child_status = -1;
  }
  
  if (child_status == Unix::fork_exec_fail)
    throw PlatformError("Launching child process failed");
  
  if (output_out)
    output_out->assign(stdout_data.begin(), stdout_data.end());
  if (output_err)
    output_err->assign(stderr_data.begin(), stderr_data.end());
  
  return child_status;
}

TemporaryPath::TemporaryPath() {
  m_data.deleted = true; // Prevent delete until we have a filename
  Unix::MallocPtr<char> name(tempnam(NULL, NULL));
  if (!name) {
    int errcode = errno;
    throw PlatformError(boost::str(boost::format("Failed to get temporary file name: %s") % Unix::error_string(errcode)));
  }
  m_path = Path(name.get());
  m_data.deleted = false;
}

TemporaryPath::~TemporaryPath() {
  delete_();
}

void TemporaryPath::delete_() {
  if (!m_data.deleted) {
    unlink(m_path.data().path.c_str());
    m_data.deleted = true;
  }
}

boost::shared_ptr<PlatformLibrary> load_library(const Path& path) {
  boost::shared_ptr<Unix::LibraryUnix> lib = boost::make_shared<Unix::LibraryUnix>(1);
  dlerror();
  void *handle = dlopen(path.data().path.c_str(), RTLD_NOW|RTLD_GLOBAL);
  if (!handle)
    throw PlatformError(boost::str(boost::format("Could not open library: %s: %s\n") % path.str() % dlerror()));
  lib->add_handle(handle);
  return lib;
}

namespace {
  void read_configuration_file(PropertyValue& pv, const Path& path) {
    std::vector<char> data;
    std::filebuf f;
    f.open(path.data().path.c_str(), std::ios::in);
    std::copy(std::istreambuf_iterator<char>(&f), std::istreambuf_iterator<char>(), std::back_inserter(data));
    if (!data.empty())
      pv.parse_configuration(&data[0], &data[0] + data.size());
  }
}

void read_configuration_files(PropertyValue& pv, const std::string& name) {
  read_configuration_file(pv, Path("/etc").join(name));
  if (const char *home = std::getenv("HOME"))
    read_configuration_file(pv, Path(home).join(".config").join(name));
}
}
}
