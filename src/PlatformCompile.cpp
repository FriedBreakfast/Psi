#include "PlatformCompile.hpp"

#include <cstring>

namespace Psi {
  namespace Platform {
    /**
     * \brief Parse a command line string into individual strings.
     */
    std::vector<std::string> split_command_line(const std::string& args) {
      std::vector<std::string> result;
      
      const char *ws = " \t\r\n";
      
      std::string current;
      for (std::string::const_iterator ii = args.begin(), ie = args.end(); ii != ie;) {
        // Eat whitespace before argument
        for (; (ii != ie) && (std::strchr(ws, *ii)); ++ii);
        
        if (ii == ie)
          break;
        
        enum {
          st_no_quote,
          st_single_quote,
          st_double_quote
        } quote_state;
        
        quote_state = st_no_quote;
        
        for (; ii != ie; ++ii) {
          if (*ii == '\\') {
            ++ii;
            if (ii == ie)
              break;
            current.push_back(*ii);
          } else if (*ii == '\'') {
            if (quote_state == st_no_quote)
              quote_state = st_single_quote;
            else if (quote_state == st_single_quote)
              quote_state = st_no_quote;
          } else if (*ii == '"') {
            if (quote_state == st_no_quote)
              quote_state = st_double_quote;
            else if (quote_state == st_double_quote)
              quote_state = st_no_quote;
          } else if ((quote_state == st_no_quote) && std::strchr(ws, *ii)) {
            break;
          } else {
            current.push_back(*ii);
          }
        }
        
        result.push_back(current);
      }
      
      return result;
    }
    
    /**
     * \brief Process linker arguments (which must be either -l or -L as for the standard Unix-like linker).
     */
    LinkerLibraryArguments parse_linker_arguments(const std::vector<std::string>& args) {
      LinkerLibraryArguments result;
      
      for (std::vector<std::string>::const_iterator ii = args.begin(), ie = args.end(); ii != ie; ++ii) {
        const std::string& s = *ii;
        if ((s.size() > 2) && (s[0] == '-')) {
          if (s[1] == 'l') {
            result.libs.push_back(s.substr(2));
          } else if (s[1] == 'L') {
            result.dirs.push_back(s.substr(2, s.find_last_not_of('/') + 1));
          } else {
            goto throw_error;
          }
        } else {
        throw_error:
          throw PlatformError("Unknown linker argument " + s);
        }
      }
      
      return result;
    }
  }
}
