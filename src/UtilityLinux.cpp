#include "Array.hpp"
#include "UtilityLinux.hpp"

#include <errno.h>
#include <string.h>

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
}
}
}
