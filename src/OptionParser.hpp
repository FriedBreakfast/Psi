#ifndef HPP_PSI_OPTION_PARSER
#define HPP_PSI_OPTION_PARSER

/**
 * \file
 * 
 * Command line parsing
 */

#include <vector>
#include <boost/unordered_map.hpp>

namespace Psi {
  struct OptionDescription {
    /// Option ID. Negative IDs are reserved.
    int key;
    /// Whether this option expects a value
    bool has_value;
    /// Short option name, or '\0'
    char short_name;
    /// Long option name
    std::string long_name;
    /// Help to be printed
    std::string help;
  };
  
  OptionDescription option_description(int key, bool has_value, char short_nam, const std::string& long_name, const std::string& help);
  
  struct OptionsDescription {
    /// Parse unknown options
    bool allow_unknown;
    /// Parse and return positional options
    bool allow_positional;
    /// Known options
    std::vector<OptionDescription> opts;
  };
  
  struct OptionValue {
    enum Keys {
      positional=-1,
      unknown=-2
    };
    /// Option key
    int key;
    /// Short name, if used, else '\0'
    char short_name;
    /// Long name, if used, else empty
    std::string long_name;
    /// Used to distinguish an absent value from an empty value; this is needed when parsing unknown arguments
    bool has_value;
    /// Value, if applicable
    std::string value;
  };
  
  void options_help(std::ostream& os, const std::string& program_name, const std::string& extra, const OptionsDescription& description);
  void options_help(const char *argv0, const std::string& extra, const OptionsDescription& description);
  void options_usage(std::ostream& os, const std::string& program_name, const std::string& extra, const std::string& help_option);
  void options_usage(const char *argv0, const std::string& extra, const std::string& help_option);
  std::string find_program_name(const char *path);
  
  class OptionParseError : public std::runtime_error {
  public:
    OptionParseError(const std::string& msg);
  };
  
  class OptionParser {
  public:
    OptionParser(const OptionsDescription& description, int argc, const char **argv);
    bool empty();
    OptionValue next();
    /// \brief Peek at the next argument on the command line
    const std::string& peek() {return m_options.back();}
    std::string take();
    
  private:
    std::vector<std::string> m_options;
    typedef boost::unordered_map<char, OptionDescription> ShortOptionMap;
    typedef boost::unordered_map<std::string, OptionDescription> LongOptionMap;
    ShortOptionMap m_short_options;
    LongOptionMap m_long_options;
    bool m_allow_unknown, m_allow_positional;
    
    std::string m_short_option_set;
    bool m_short_option_value_set;
    std::string m_short_option_value;
    
    OptionValue short_option_next();
  };
}

#endif
