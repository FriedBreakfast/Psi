#include "PlatformUnix.hpp"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

namespace Psi {
namespace Platform {
namespace Unix {
#if PSI_WITH_EXEC
pid_t sys_fork_exec(int stdin_fd, int stdout_fd, int stderr_fd, char *const*args_ptr) {
  pid_t child_pid = vfork();
  if (child_pid == 0) {
    // In the child
    int fds3[3] = {stdin_fd, stdout_fd, stderr_fd};
    for (int i = 0; i < 3; ++i) {
      if (fds3[i] != i) {
        // Note dup2 does not clone the close-on-exec flag
        if (dup2(fds3[i], i) < 0)
          _exit(fork_exec_fail);
      } else {
        fcntl(i, F_SETFD, 0);
      }
    }
    
    closefrom(3);
    
    execvp(args_ptr[0], args_ptr);
    _exit(fork_exec_fail);
  } else {
    return child_pid;
  }
}

int sys_pipe(int fds[2]) {
  return pipe(fds);
}
#endif

char* sys_strerror_r(int errnum, char *buf, size_t buflen) {
  int code = strerror_r(errnum, buf, buflen);
  return (code == 0) ? buf : NULL;
}

}
}
}
