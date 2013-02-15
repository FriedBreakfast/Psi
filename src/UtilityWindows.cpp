#include "UtilityWindows.hpp"

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

void LibraryHandle::swap(LibHandle& other) {
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

PSI_ATTRIBUTE((PSI_NORETURN)) void throw_error(DWORD error)  {
  throw PlatformError(error_string(error));
}

PSI_ATTRIBUTE((PSI_NORETURN)) void throw_last_error() {
  throw_error(GetLastError());
}
}
}
}
