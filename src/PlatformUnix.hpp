#ifndef HPP_PSI_PLATFORM_LINUX
#define HPP_PSI_PLATFORM_LINUX

#include <string>
#include <sys/types.h>

#include "Platform.hpp"

namespace Psi {
namespace Platform {
namespace Unix {
namespace {
/// Exit code of processes which fail after fork()
const int fork_exec_fail = 127;
}

std::string error_string(int errcode);

class PSI_COMPILER_COMMON_EXPORT LibraryUnix : public PlatformLibrary {
  std::vector<void*> m_handles;

public:
  LibraryUnix(unsigned hint=0);
  virtual ~LibraryUnix();
  virtual boost::optional<void*> symbol(const std::string& symbol);
  void add_handle(void *handle);
};

/**
 * Fork and execute a process with the given file descriptors attached to
 * stdin, stdout and stderr.
 */
pid_t sys_fork_exec(int stdin_fd, int stdout_fd, int stderr_fd, char *const*args_ptr);
/// Pipe according to the local platform
int sys_pipe(int fds[2]);
/// strerror_r, adapted to GNU convention
char* sys_strerror_r(int errnum, char *buf, size_t buflen);
}
}
}

#endif
