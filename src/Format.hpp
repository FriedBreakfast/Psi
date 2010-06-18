#ifndef HPP_PSI_FORMAT
#define HPP_PSI_FORMAT

#include <boost/format.hpp>

namespace Psi {
  namespace Format {
    template<typename Formatter>
    void format_insert(Formatter&) {
    }

    template<typename Formatter, typename T, typename... Args>
    void format_insert(Formatter& fmt, T&& first, Args&&... args) {
      fmt % std::forward<T>(first);
      format_insert(fmt, std::forward<Args>(args)...);
    }
  }

  template<typename... Args>
  std::string format(const char *fmt, Args&&... args) {
    boost::basic_format<char> formatter(fmt);
    Format::format_insert(formatter, std::forward<Args>(args)...);
    return formatter.str();
  }

  template<typename... Args>
  std::string format(const std::string& fmt, Args&&... args) {
    return format(fmt.c_str(), std::forward<Args>(args)...);
  }
}

#endif
