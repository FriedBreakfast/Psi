#include "PlatformWindows.hpp"
#include "Platform.hpp"
#include "Array.hpp"
#include "Utility.hpp"
#include "PropertyValue.hpp"

#include <fstream>
#include <cctype>

#include <process.h>
#include <Shlwapi.h>
#include <ShlObj.h>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/scoped_array.hpp>

namespace Psi {
namespace Platform {
namespace Windows {
/**
 * \brief Convert a UTF8 string to WCHARs.
 */
std::wstring utf8_to_wchar(const char *s, std::size_t n) {
  if (n == 0)
    return std::wstring();

  const unsigned data_length = 128;
  SmallArray<WCHAR, data_length> data;
  data.resize(data_length);
  int retval = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, data.get(), data.size());
  if (retval == 0)
    throw_last_error();

  if (retval <= (int)data.size())
    return std::wstring(data.get(), data.get() + retval);

  unsigned length = retval;
  boost::scoped_array<WCHAR> data_alloc(new WCHAR[length]);
  retval = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, data_alloc.get(), length);
  if (retval == 0)
    throw_last_error();
  else if (retval != length)
    throw PlatformError("Error converting UTF-8 string to UTF-16: could not determine number of bytes required");

  return std::wstring(data_alloc.get(), data_alloc.get()+length);
}

std::wstring utf8_to_wchar(const std::string& s) {
  if (s.empty())
    return std::wstring();
  return utf8_to_wchar(s.c_str(), s.empty());
}

/**
 * \brief Convert a WCHAR string to UTF8.
 */
std::string wchar_to_utf8(const WCHAR *begin, std::size_t n) {
  if (n == 0)
    return std::string();

  const unsigned data_length = 128;
  SmallArray<char, data_length> data;
  data.resize(data_length);
  int retval = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, begin, n, data.get(), data.size(), NULL, NULL);
  if (retval == 0)
    throw_last_error();

  if (retval > (int)data.size()) {
    data.resize(retval);
    retval = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, begin, n, data.get(), data.size(), NULL, NULL);
    if (retval == 0)
      throw_last_error();
    else if (retval != data.size())
      throw PlatformError("Error converting UTF-16 string to UTF-8: could not determine number of bytes required");
  }

  return std::string(data.get(), data.get()+data.size());
}

std::string wchar_to_utf8(const WCHAR *str) {
  return wchar_to_utf8(str, wcslen(str));
}

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
  LocalPtr<WCHAR> message;

  DWORD result = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&message.ptr, 0, NULL);

  if (!result)
    return "Unknown error";

  return wchar_to_utf8(message.ptr);
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

std::string hresult_error_string(HRESULT error) {
  LocalPtr<WCHAR> message;
  DWORD result = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&message.ptr, 0, NULL);

  if (!result)
    return "Unknown error";

  return wchar_to_utf8(message.ptr);
}
}

TemporaryPath::TemporaryPath() {
  WCHAR path_buffer[MAX_PATH+1], file_buffer[MAX_PATH+1];
  DWORD retval = GetTempPathW(MAX_PATH, path_buffer);
  if (retval == 0)
    Windows::throw_last_error();

  GetTempFileNameW(path_buffer, L"tmp", 0, file_buffer);
  if (retval == 0)
    Windows::throw_last_error();

  m_path = Path(std::wstring(file_buffer));
}

TemporaryPath::~TemporaryPath() {
  delete_();
}

void TemporaryPath::delete_() {
  if (!m_data.deleted)
    DeleteFileW(m_path.data().c_str());
}

namespace Windows {
void argument_convert_char(std::vector<WCHAR>& output, std::string::const_iterator ii, std::string::const_iterator ie) {
  unsigned count, value;
  unsigned char first = *ii;
  if (first >= 0xFC) {count = 5; value = first & 0x1;}
  else if (first >= 0xF8) {count = 4; value = first & 0x3;}
  else if (first >= 0xF0) {count = 3; value = first & 0x7;}
  else if (first >= 0xE0) {count = 2; value = first & 0xF;}
  else if (first >= 0xC0) {count = 1; value = first & 0x1F;}
  else {
    if (first >= 0x80)
      throw PlatformError("Invalid UTF-8 sequence");
    output.push_back(first);
    ++ii;
    return;
  }

  for (; count > 0; --count) {
    ++ii;
    if (ii == ie)
      throw PlatformError("Invalid UTF-8 sequence");
    value = (value << 6) | (*ii & 0x3F);
  }

  ++ii;

  if (value < 0x10000) {
    if ((value >= 0xD800) && (value < 0xE000))
      throw PlatformError("Invalid Unicode: code point in surrogate range encountered");
    output.push_back(value);
  } else if (value < 0x20000) {
    unsigned subtracted = value - 0x10000;
    output.push_back(0xD800 | ((value >> 10) & 0x3F));
    output.push_back(0xDC00 | (value & 0x3F));
  } else {
    throw PlatformError("Cannot encode Unicode code point above 0x20000 to UTF-16");
  }
}

void argument_convert_char(std::vector<WCHAR>& output, std::wstring::const_iterator ii, std::wstring::const_iterator ie) {
  output.push_back(*ii);
  ++ii;
}

template<typename CharT, typename StringType>
void escape_argument(std::vector<WCHAR>& output, const StringType& str, CharT slash, CharT quote) {
  bool has_whitespace = false;
  for (typename StringType::const_iterator ji = str.begin(), je = str.end(); ji != je; ++ji) {
    if (std::isspace(*ji)) {
      has_whitespace = true;
      break;
    }
  }

  if (has_whitespace)
    output.push_back(L'\"');

  unsigned backslash_count = 0;
  for (typename StringType::const_iterator ji = str.begin(), je = str.end(); ji != je; ++ji) {
    if (*ji == slash) {
      argument_convert_char(output, ji, je);
      ++backslash_count;
    } else if (*ji == quote) {
      output.insert(output.end(), 2*backslash_count+1, L'\\');
      output.push_back('\"');
      backslash_count = 0;
    } else {
      output.insert(output.end(), backslash_count, L'\\');
      output.push_back(*ji);
      backslash_count = 0;
    }
  }

  if (has_whitespace) {
    output.insert(output.end(), 2*backslash_count, L'\\');
    output.push_back(L'\"');
  } else {
    output.insert(output.end(), backslash_count, L'\\');
  }
}

/**
 * Take a series of command line arguments and create a string which Windows will
 * parse into the same list of arguments.
 */
std::vector<WCHAR> escape_command_line(const Path& command, const std::vector<std::string>& args) {
  std::vector<WCHAR> result;
  escape_argument(result, command.data(), L'\\', L'\"');
  for (std::vector<std::string>::const_iterator ib = args.begin(), ii = args.begin(), ie = args.end(); ii != ie; ++ii) {
    result.push_back(L' ');
    escape_argument(result, *ii, '\\', '\"');
  }
  result.push_back(L'\0');
  return result;
}
}

namespace {
template<typename T=HANDLE>
class Handle : public NonCopyable {
public:
  HANDLE handle;
  Handle() : handle(NULL) {}
  Handle(T h) : handle(h) {}

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

int exec_communicate(const Path& command, const std::vector<std::string>& args,
                     const std::string& input, std::string *output_out, std::string *output_err) {
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
  STARTUPINFOW start_info;
  ZeroMemory(&start_info, sizeof(STARTUPINFOW));
  start_info.cb = sizeof(STARTUPINFOW);
  start_info.hStdInput = stdin_read.handle;
  start_info.hStdOutput = stdout_write.handle;
  start_info.hStdError = stderr_write.handle;
  start_info.dwFlags = STARTF_USESTDHANDLES;

  std::vector<WCHAR> cmdline = Windows::escape_command_line(command, args);
  if (!CreateProcessW(NULL, &cmdline[0], NULL, NULL, TRUE, 0, NULL, NULL, &start_info, &proc_info))
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

Path::Path() {
}

Path::Path(const std::string& pth) : m_data(Windows::utf8_to_wchar(pth)) {
}

Path::Path(const PathData& data) : m_data(data) {
}

Path::~Path() {
}

std::string Path::str() const {
  return Windows::wchar_to_utf8(m_data.c_str(), m_data.size());
}

Path Path::join(const Path& second) const {
  WCHAR buffer[MAX_PATH+1];
  if (!PathCombineW(buffer, m_data.c_str(), second.m_data.c_str()))
    Windows::throw_last_error();
  return Path(std::wstring(buffer));
}

Path Path::normalize() const {
  WCHAR buffer[MAX_PATH+1];
  if (!PathCanonicalizeW(buffer, m_data.c_str()))
    Windows::throw_last_error();
  return Path(std::wstring(buffer));
}

Path Path::absolute() const {
  if (PathIsRelativeW(m_data.c_str()))
    return getcwd().join(*this);
  else
    return *this;
}

Path Path::filename() const {
  if (m_data.size() > MAX_PATH)
    throw PlatformError("Path too long");
  WCHAR buffer[MAX_PATH+1];
  PathStripPathW(buffer);
  return Path(std::wstring(buffer));
}

Path getcwd() {
  SmallArray<WCHAR, MAX_PATH> buffer;
  buffer.resize(MAX_PATH);

  while (true) {
    DWORD result = GetCurrentDirectoryW(buffer.size(), buffer.get());
    if (result == 0)
      Windows::throw_last_error();
    else if (result <= buffer.size())
      return Path(std::wstring(buffer.get(), buffer.get()+result));
    buffer.resize(result);
  }
}

boost::optional<Path> find_in_path(const Path& name) {
  if (name.data().find_first_of(L"/\\") != std::string::npos) {
    Path abs_path = name.absolute();
    if (PathFileExistsW(abs_path.data().c_str()))
      return abs_path;
    return boost::none;
  } else {
    WCHAR buffer[MAX_PATH+1];
    if (name.data().size() > MAX_PATH)
      throw PlatformError("Path too long");
    std::copy(name.data().begin(), name.data().end(), buffer);
    buffer[name.data().size()] = L'\0';

    if (PathFindOnPathW(buffer, NULL)) {
      return Path(std::wstring(buffer));
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

boost::shared_ptr<PlatformLibrary> load_library(const Path& path) {
  boost::shared_ptr<Windows::LibraryWindows> lib = boost::make_shared<Windows::LibraryWindows>(1);
  HMODULE handle = LoadLibraryW(path.data().c_str());
  if (!handle)
    Windows::throw_last_error();
  lib->add_handle(handle);
  return lib;
}

namespace {
std::vector<char> load_file(HANDLE hfile) {
  std::size_t data_offset = 0;
  std::vector<char> data(1024);
  while (true) {
    DWORD count_out;
    if (!ReadFile(hfile, &data[data_offset], data.size() - data_offset, &count_out, NULL))
      Windows::throw_last_error();
    data_offset += count_out;
    if (data_offset < data.size()) {
      data.resize(data_offset);
      return data;
    }
    data.resize(data.size()*2);
  }
}
}

void read_configuration_files(PropertyValue& pv, const std::string& name) {
  const unsigned n_folders = 3;
  int folders_idxs[n_folders] = {
    CSIDL_COMMON_APPDATA,
    CSIDL_APPDATA,
    CSIDL_LOCAL_APPDATA
  };

  Path name_path(name);

  WCHAR data[MAX_PATH+1];
  for (unsigned i = 0; i < n_folders; ++i) {
    HRESULT res = SHGetFolderPathW(NULL, 0, NULL, SHGFP_TYPE_CURRENT, data);
    if (res == E_FAIL)
      continue;
    else if (res != S_OK)
      PSI_NOT_IMPLEMENTED();

    Path full_path = Path(data).join(name_path);
    Handle<> file_handle(CreateFileW(full_path.data().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
    if (file_handle.handle == INVALID_HANDLE_VALUE)
      continue;

    std::vector<char> data = load_file(file_handle.handle);
    file_handle.close();

    if (!data.empty())
      pv.parse_configuration(&data[0], &data[0] + data.size());
  }
}
}
}
