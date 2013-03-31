#include <string>
#include <vector>
#include <boost/format.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>

#include "../../Utility.hpp"
#include "../../UtilityLinux.hpp"
#include "../../ErrorContext.hpp"

namespace Psi {
namespace Tvm {
namespace CBackend {
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
  
  int fd() {return m_fd;}
  void set_fd(int fd) {close(); m_fd = fd;}
  
  void close() {
    if (m_fd >= 0) {
      ::close(m_fd);
      m_fd = -1;
    }
  }
};

void cmd_pipe(CompileErrorPair& err_loc, FileDescriptor& read, FileDescriptor& write) {
  int p[2];
  if (::pipe(p) != 0) {
    int errcode = errno;
    err_loc.error_throw(boost::format("Failed to create pipe for interprocess communication: %s") % Platform::Linux::error_string(errcode), CompileError::error_internal);
  }
  read.set_fd(p[0]);
  write.set_fd(p[1]);
}

void cmd_set_nonblock(CompileErrorPair& err_loc, int fd) {
  int err = fcntl(fd, F_SETFL, O_NONBLOCK);
  if (err < 0) {
    int errcode = errno;
    err_loc.error_throw(boost::format("Failed to set up nonblocking I/O mode for interprocess communication: %s") % Platform::Linux::error_string(errcode), CompileError::error_internal);
  }
}

/**
 * Read data from a file descriptor into a buffer until end-of-file, or EAGAIN is returned.
 * If end-of-file is reached, the file is closed.
 * 
 * \return True if more data is available.
 */
bool cmd_read_by_buffer(CompileErrorPair& err_loc, FileDescriptor& fd, std::vector<char>& buffer, std::vector<char>& output) {
  while (true) {
    int n = read(fd.fd(), vector_begin_ptr(buffer), buffer.size());
    if (n == 0) {
      fd.close();
      return false;
    } else if (n < 0) {
      int errcode = errno;
      if (errcode == EAGAIN)
        return true;
      else
        err_loc.error_throw(boost::format("Failed to read from pipe during interprocess communication: %s") % Platform::Linux::error_string(errcode), CompileError::error_internal);
    } else {
      output.insert(output.end(), buffer.begin(), buffer.begin() + n);
    }
  }
}

bool cmd_write_by_buffer(CompileErrorPair& err_loc, FileDescriptor& fd, const char*& ptr, const char *end) {
  int n = write(fd.fd(), ptr, end-ptr);
  if (n < 0) {
    int errcode = errno;
    if (errcode == EAGAIN)
      return true;
    else
      err_loc.error_throw(boost::format("Failed to write to pipe during interprocess communication: %s") % Platform::Linux::error_string(errcode), CompileError::error_internal);
  } else {
    ptr += n;
    if (ptr == end) {
      fd.close();
      return false;
    }
    
    return true;
  }
}

/// Convert a std::string to a C string, allocating the array with new[]
char* cmd_string_to_c(const std::string& s) {
  char *copy = new char [s.size()+1];
  std::copy(s.begin(), s.end(), copy);
  copy[s.size()] = '\0';
  return copy;
}

/**
 * Allocator for boost::ptr_vector which handles C strings.
 */
struct StringCloneAllocator {
  static void deallocate_clone(const char *ptr) {
    delete [] const_cast<char*>(ptr);
  }
};
}

/**
 * Run a command and send data to its standard in, capture standard out and return the result.
 */
void cmd_communicate(CompileErrorPair& err_loc, const std::vector<std::string>& command, const std::string& input, int expected_status) {
  // Read/write direction refers to the parent process
  FileDescriptor stdin_read, stdin_write, stdout_read, stdout_write, stderr_read, stderr_write;
  cmd_pipe(err_loc, stdin_read, stdin_write);
  cmd_pipe(err_loc, stdout_read, stdout_write);
  cmd_pipe(err_loc, stderr_read, stderr_write);
  
  boost::ptr_vector<char, StringCloneAllocator> args;
  for (std::vector<std::string>::const_iterator ii = command.begin(), ie = command.end(); ii != ie; ++ii)
    args.push_back(cmd_string_to_c(*ii));
  args.push_back(NULL);
  
  pid_t child_pid = fork();
  if (child_pid == 0) {
    // Child process
    if (dup2(stdin_read.fd(), 0) < 0) _exit(1);
    if (dup2(stdout_write.fd(), 1) < 0) _exit(1);
    if (dup2(stderr_write.fd(), 2) < 0) _exit(1);
    
    stdin_read.close();
    stdin_write.close();
    stdout_read.close();
    stdout_write.close();
    stderr_read.close();
    stderr_write.close();
    
    char *const* args_ptr = args.c_array();
    execvp(args_ptr[0], args_ptr);
    _exit(1);
  }
  
  // Parent process
  stdin_read.close();
  stdout_write.close();
  stderr_write.close();
  
  cmd_set_nonblock(err_loc, stdin_write.fd());
  cmd_set_nonblock(err_loc, stdout_read.fd());
  cmd_set_nonblock(err_loc, stderr_read.fd());
  
  fd_set readfds, writefds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_SET(stdin_write.fd(), &writefds);
  FD_SET(stdout_read.fd(), &readfds);
  FD_SET(stderr_read.fd(), &readfds);
  
  std::vector<char> buffer(1024);
  std::vector<char> stdout_data, stderr_data;
  std::vector<char> stdin_data(input.begin(), input.end());
  const char *write_ptr = vector_begin_ptr(stdin_data), *write_end = vector_end_ptr(stdin_data);

  while (true) {
    int nfds = -1;
    
    if (FD_ISSET(stdin_write.fd(), &writefds)) {
      if (cmd_write_by_buffer(err_loc, stdin_write, write_ptr, write_end)) {
        nfds = std::max(stdin_write.fd(), nfds);
        FD_SET(stdin_write.fd(), &writefds);
      } else {
        FD_CLR(stdin_write.fd(), &writefds);
      }
    }
    
    if (FD_ISSET(stdout_read.fd(), &readfds)) {
      if (cmd_read_by_buffer(err_loc, stdout_read, buffer, stdout_data)) {
        nfds = std::max(stdout_read.fd(), nfds);
        FD_SET(stdout_read.fd(), &readfds);
      } else {
        FD_CLR(stdout_read.fd(), &readfds);
      }
    }
    
    if (FD_ISSET(stderr_read.fd(), &readfds)) {
      if (cmd_read_by_buffer(err_loc, stderr_read, buffer, stderr_data)) {
        nfds = std::max(stderr_read.fd(), nfds);
        FD_SET(stderr_read.fd(), &readfds);
      } else {
        FD_CLR(stderr_read.fd(), &readfds);
      }
    }
    
    if (nfds < 0)
      break;

    int err = select(nfds+1, &readfds, &writefds, NULL, NULL);
    if (err < 0) {
      int errcode = errno;
      err_loc.error_throw(boost::format("Failure during interprocess communication in select(): %s") % Platform::Linux::error_string(errcode), CompileError::error_internal);
    }
  }
  
  int child_status;
  if (waitpid(child_pid, &child_status, 0) == -1) {
    int errcode = errno;
    err_loc.error_throw(boost::format("Could not get child process exit status: %s") % Platform::Linux::error_string(errcode), CompileError::error_internal);
  }
  
  if (child_status != expected_status)
    err_loc.error_throw(boost::format("Child process failed (exit status %d): %s") % child_status % command.front(), CompileError::error_internal);
}
}
}
}
