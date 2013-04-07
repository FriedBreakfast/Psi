#include "PlatformWindows.hpp"
#include "Platform.hpp"
#include "Array.hpp"

#include <cctype>
#include <process.h>
#include <Shlwapi.h>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>

namespace Psi {
namespace Platform {
namespace Windows {
LibraryHandle::LibraryHandle() : m_handle(NULL) {
}

LibraryHandle::LibraryHandle(HMODULE handle) : m_handle(handle) {
}

LibraryHandle::~LibraryHandle() {
  if (m_handle)
    FreeLibrary(m_handle);
}

void LibraryHandle::swap(LibraryHandle& other) {
  std::swap(m_handle, other.m_handle);
}

/**
 * Convert a Win32 error code to a string, via FormatMessage.
 */
std::string error_string(DWORD error) {
  LocalPtr<CHAR> message;

  DWORD result = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&message.ptr, 0, NULL);

  if (!result)
    return "Unknown error";

  return std::string(message.ptr);
}

/**
 * Description of last error, i.e.
 * \code error_string(GetLastError()) \endcode
 */
std::string last_error_string() {
  return error_string(GetLastError());
}

/**
 * Throw the last Windows error as an exception.
 */
PSI_ATTRIBUTE((PSI_NORETURN)) void throw_last_error() {
  throw PlatformError(last_error_string());
}
}

namespace Windows {
class TemporaryPathWindows : public TemporaryPathImpl {
public:
  TemporaryPathWindows(const std::string& path) : TemporaryPathImpl(path) {}

  virtual void delete_() {
    DeleteFile(path().c_str());
  }
};
}

TemporaryPathImpl* make_temporary_path_impl() {
  std::vector<char> path_buffer(MAX_PATH+1), file_buffer(MAX_PATH+1);
  DWORD retval = GetTempPath(path_buffer.size(), &path_buffer[0]);
  if (retval == 0)
    Windows::throw_last_error();
  path_buffer.back() = '\0'; // Ensure string is NULL terminated

  GetTempFileName(&path_buffer[0], "tmp", 0, &file_buffer[0]);
  if (retval == 0)
    Windows::throw_last_error();
  path_buffer.back() = '\0';

  return new Windows::TemporaryPathWindows(std::string(&file_buffer[0]));
}

namespace Windows {
/**
 * Take a series of command line arguments and create a string which Windows will
 * parse into the same list of arguments.
 */
std::vector<CHAR> unescape_command_line(const std::vector<std::string>& command) {
  std::vector<CHAR> result;
  for (std::vector<std::string>::const_iterator ib = command.begin(), ii = command.begin(), ie = command.end(); ii != ie; ++ii) {
    if (ii != ib) result.push_back(' ');
    bool has_whitespace = false;
    for (std::string::const_iterator ji = ii->begin(), je = ii->end(); ji != je; ++ji) {
      if (std::isspace(*ji)) {
        has_whitespace = true;
        break;
      }
    }

    if (has_whitespace)
      result.push_back('\"');

    unsigned backslash_count = 0;
    for (std::string::const_iterator ji = ii->begin(), je = ii->end(); ji != je; ++ji) {
      if (*ji == '\\') {
        ++backslash_count;
      } else if (*ji == '\"') {
        result.insert(result.end(), 2*backslash_count+1, '\\');
        result.push_back('\"');
        backslash_count = 0;
      } else {
        result.insert(result.end(), backslash_count, '\\');
        result.push_back(*ji);
        backslash_count = 0;
      }
    }

    if (has_whitespace) {
      result.insert(result.end(), 2*backslash_count, '\\');
      result.push_back('\"');
    } else {
      result.insert(result.end(), backslash_count, '\\');
    }
  }
  result.push_back('\0');
  return result;
}
}

namespace {
template<typename T=HANDLE>
class Handle {
public:
  HANDLE handle;
  Handle() : handle(NULL) {}

  ~Handle() {
    close();
  }

  void close() {
    if (handle) {
      CloseHandle(handle);
      handle = NULL;
    }
  }
};

class PipeReadThread {
  HANDLE m_pipe, m_thread;
  DWORD m_error_code;
  std::vector<char> m_data;

  static DWORD WINAPI thread_main(LPVOID parameter) {
    PipeReadThread& self = *static_cast<PipeReadThread*>(parameter);
    const unsigned buffer_size = 4096;
    CHAR buffer[buffer_size];

    while (true) {
      DWORD bytes_read;
      BOOL success = ReadFile(self.m_pipe, buffer, buffer_size, &bytes_read, NULL);
      if (!success) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE) {
          return 0;
        } else {
          self.m_error_code = err;
          return 1;
        }
      }

      if (bytes_read == 0)
        return 0;

      self.m_data.insert(self.m_data.end(), buffer, buffer + bytes_read);
    }
  }

public:
  PipeReadThread(HANDLE pipe) : m_pipe(pipe) {
    m_thread = CreateThread(NULL, 0, thread_main, this, 0, NULL);
    if (!m_thread)
      Windows::throw_last_error();
  }

  ~PipeReadThread() {
    if (m_thread)
      CloseHandle(m_thread);
  }

  void wait() {
    DWORD wait_result = WaitForSingleObject(m_thread, INFINITE);
    if (wait_result == WAIT_FAILED)
      Windows::throw_last_error();
    else if (wait_result == WAIT_ABANDONED)
      throw PlatformError("Thread wait abandoned unexpectedly");
    else if (wait_result == WAIT_TIMEOUT)
      throw PlatformError("Thread wait failed due to timeout");
    else if (wait_result != WAIT_OBJECT_0)
      throw PlatformError(boost::str(boost::format("Thread wait failed for unknown reason: %d") % wait_result));

    DWORD exit_code;
    if (!GetExitCodeThread(m_thread, &exit_code))
      Windows::throw_last_error();

    CloseHandle(m_thread);
    m_thread = NULL;

    if (exit_code != 0)
      throw PlatformError((m_error_code != ERROR_SUCCESS) ? Windows::error_string(m_error_code) : "Unknown failure reading from pipe");
  }

  const std::vector<char>& data() {
    return m_data;
  }
};
}

int exec_communicate(const std::vector<std::string>& command, const std::string& input, std::string *output_out, std::string *output_err) {
  SECURITY_ATTRIBUTES pipe_security_attr;
  pipe_security_attr.nLength = sizeof(pipe_security_attr);
  pipe_security_attr.bInheritHandle = TRUE;
  pipe_security_attr.lpSecurityDescriptor = NULL;
  Handle<> stdin_write, stdin_read, stdout_write, stdout_read, stderr_write, stderr_read;

  if (!CreatePipe(&stdin_read.handle, &stdin_write.handle, &pipe_security_attr, 0) ||
      !CreatePipe(&stdout_read.handle, &stdout_write.handle, &pipe_security_attr, 0) ||
      !CreatePipe(&stderr_read.handle, &stderr_write.handle, &pipe_security_attr, 0)) {
    Windows::throw_last_error();
  }

  if (!SetHandleInformation(stdin_write.handle, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(stdout_read.handle, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(stderr_read.handle, HANDLE_FLAG_INHERIT, 0)) {
    Windows::throw_last_error();
  }

  //if (!SetHandle

  PROCESS_INFORMATION proc_info;
  ZeroMemory(&proc_info, sizeof(proc_info));
  STARTUPINFO start_info;
  ZeroMemory(&start_info, sizeof(STARTUPINFO));
  start_info.cb = sizeof(STARTUPINFO);
  start_info.hStdInput = stdin_read.handle;
  start_info.hStdOutput = stdout_write.handle;
  start_info.hStdError = stderr_write.handle;
  start_info.dwFlags = STARTF_USESTDHANDLES;

  std::vector<CHAR> cmdline = Windows::unescape_command_line(command);
  if (!CreateProcess(NULL, &cmdline[0], NULL, NULL, TRUE, 0, NULL, NULL, &start_info, &proc_info))
    Windows::throw_last_error();
  Handle<> child_process, child_thread;
  child_process.handle = proc_info.hProcess;
  child_thread.handle = proc_info.hThread;

  // So... Windows doesn't support asynchronous I/O on anonymous pipes and nonblocking
  // I/O on pipes is deprecated. Threads it is then!
  stdin_read.close();
  stdout_write.close();
  stderr_write.close();

  PipeReadThread stdout_thread(stdout_read.handle);
  PipeReadThread stderr_thread(stderr_read.handle);

  DWORD bytes_written;
  if (!WriteFile(stdin_write.handle, input.c_str(), input.length(), &bytes_written, NULL))
    Windows::throw_last_error();
  PSI_ASSERT(bytes_written == input.length());
  stdin_write.close();

  stdout_thread.wait();
  stderr_thread.wait();

  DWORD exit_code;
  if (WaitForSingleObject(proc_info.hProcess, INFINITE) != WAIT_OBJECT_0)
    Windows::throw_last_error();
  if (!GetExitCodeProcess(proc_info.hProcess, &exit_code))
    Windows::throw_last_error();

  if (output_out)
    output_out->assign(stdout_thread.data().begin(), stdout_thread.data().end());
  if (output_err)
    output_err->assign(stderr_thread.data().begin(), stderr_thread.data().end());

  return exit_code;
}

namespace Windows {
std::string getcwd() {
  const unsigned buffer_size = 64;
  char buffer[buffer_size];
  DWORD result = GetCurrentDirectory(buffer_size, buffer);
  if ((result > 0) && (result <= buffer_size))
    return std::string(buffer, buffer+result);
  else if (result == 0)
    throw_last_error();

  std::vector<char> heap_buffer;
  while (true) {
    heap_buffer.resize(result);
    result = GetCurrentDirectory(heap_buffer.size(), &heap_buffer[0]);
    if (result == 0)
      throw_last_error();
    else if (result == heap_buffer.size())
      return std::string(heap_buffer.begin(), heap_buffer.end());
  }
}
}

std::string join_path(const std::string& first, const std::string& second) {
  CHAR buffer[MAX_PATH+1];
  if (!PathCombine(buffer, first.c_str(), second.c_str()))
    Windows::throw_last_error();
  return buffer;
}

std::string normalize_path(const std::string& path) {
  CHAR buffer[MAX_PATH+1];
  if (!PathCanonicalize(buffer, path.c_str()))
    Windows::throw_last_error();
  return buffer;
}

std::string absolute_path(const std::string& path) {
  if (PathIsRelative(path.c_str()))
    return join_path(Windows::getcwd(), path);
  else
    return path;
}

std::string filename(const std::string& path) {
  CHAR copy[MAX_PATH+1];
  if (path.size() > MAX_PATH)
    throw PlatformError("Path too long");
  std::copy(path.begin(), path.end(), copy);
  copy[path.size()] = '\0';
  PathStripPath(copy);
  return copy;
}

boost::optional<std::string> find_in_path(const std::string& name) {
  if (name.find_first_of("/\\") != std::string::npos) {
    std::string abs_path = absolute_path(name);
    if (PathFileExists(abs_path.c_str()))
      return abs_path;
    return boost::none;
  } else {
    CHAR buffer[MAX_PATH+1];
    if (name.size() > MAX_PATH)
      throw PlatformError("Path too long");
    buffer[name.size()] = '\0';

    if (PathFindOnPath(buffer, NULL)) {
      buffer[MAX_PATH] = '\0';
      return buffer;
    } else {
      return boost::none;
    }
  }
}

namespace Windows {
LibraryWindows::LibraryWindows(unsigned hint) {
  m_handles.reserve(hint);
}

LibraryWindows::~LibraryWindows() {
  while (!m_handles.empty()) {
    FreeLibrary(m_handles.back());
    m_handles.pop_back();
  }
}

boost::optional<void*> LibraryWindows::symbol(const std::string& symbol) {
  for (std::vector<HMODULE>::const_reverse_iterator ii = m_handles.rbegin(), ie = m_handles.rend(); ii != ie; ++ii) {
    if (void *ptr = GetProcAddress(*ii, symbol.c_str()))
      return ptr;
  }
  
  return boost::none;
}

/**
 * Take ownership of a handle, and add it to this library..
 */
void LibraryWindows::add_handle(HMODULE handle) {
  try {
    m_handles.push_back(handle);
  } catch (...) {
    FreeLibrary(handle);
    throw;
  }
}
}

boost::shared_ptr<PlatformLibrary> load_library(const std::string& path) {
  boost::shared_ptr<Windows::LibraryWindows> lib = boost::make_shared<Windows::LibraryWindows>(1);
  HMODULE handle = LoadLibrary(path.c_str());
  if (!handle)
    Windows::throw_last_error();
  lib->add_handle(handle);
  return lib;
}
}
}
