#include "PlatformUnix.hpp"

#include <sys/syscall.h>
#include <fcntl.h>
#include <string.h>

namespace Psi {
namespace Platform {
namespace Unix {
#if PSI_WITH_EXEC
namespace {
struct linux_dirent {
  long           d_ino;
  off_t          d_off;
  unsigned short d_reclen;
  char           d_name[];
};

// Avoid locale dependency of strtol
int safe_atoi(const char *s) {
  int val = 0;
  for (; *s != '\0'; ++s) {
    val = val * 10;
    val += (*s - '0');
  }
  return val;
}

int close_on_exec_all() {
  const unsigned buffer_size = 1024;
  char buffer[buffer_size];
  int nbytes, offset;
  linux_dirent *ent;

  int proc_fd = open("/proc/self/fd", O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  if (!proc_fd)
    return -1;

  while (1) {
    nbytes = syscall(SYS_getdents, proc_fd, &buffer, buffer_size);
    if (nbytes <= 0) {
      close(proc_fd);
      return nbytes;
    }
    
    for (offset = 0; offset < nbytes; offset += ent->d_reclen) {
      ent = (linux_dirent*)(buffer + offset);
      fcntl(safe_atoi(ent->d_name), F_SETFD, FD_CLOEXEC);
    }
  }
}
}

/**
 * This is written in C because vfork() on Linux actually does the sensible thing and can be used *provided*
 * that we take care not to overwrite memory in the parent process. The vfork() child and parent do not
 * share a file descriptor table so the dup2() will work. The following code is therefore not POSIX
 * compliant since POSIX doesn't guarantee an system calls other than execvp and _exit.
 * 
 * Note that this assumes stdin_fd, stdout_fd and stderr_fd have the close-on-exec flag set unless they
 * are < 3. Also note that swapping stdout_fd and stderr_fd will not work.
 */
pid_t sys_fork_exec(int stdin_fd, int stdout_fd, int stderr_fd, char *const*args_ptr) {
  pid_t child_pid = vfork();
  if (child_pid == 0) {
    if (close_on_exec_all() != 0)
      _exit(fork_exec_fail);

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
    
    execvp(args_ptr[0], args_ptr);
    _exit(fork_exec_fail);
  } else {
    return child_pid;
  }
}

int sys_pipe(int fds[2]) {
  return pipe2(fds, O_CLOEXEC);
}
#endif

char* sys_strerror_r(int errnum, char *buf, size_t buflen) {
#if !defined(__ANDROID__)
  return strerror_r(errnum, buf, buflen);
#else
  int code = strerror_r(errnum, buf, buflen);
  return (code == 0) ? buf : NULL;
#endif
}
}
}
}
