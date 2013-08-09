#include "Assert.hpp"
#include "OptionParser.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <boost/format.hpp>

namespace Psi {
namespace {
  /// Number of columns to use to print help
  const unsigned options_help_columns = 80;
  /// Indentation of option description paragraphs.
  const unsigned options_help_indent = 15;
}

/**
 * \brief Get the program name used in error messages from a path.
 * 
 * \param path Path to this command, usually argv[0]
 */
std::string find_program_name(const char *path) {
  std::string exe_name(path);
  std::size_t name_pos = exe_name.find_last_of("/\\");
  if (name_pos != std::string::npos)
    exe_name = exe_name.substr(name_pos + 1);
  return exe_name;
}

void options_help(std::ostream& os, const std::string& program_name, const std::string& extra, const OptionsDescription& options) {
  os << "Usage:\n"
     << " " << program_name << " [options]" << extra << '\n';
  os << "Options:\n";
  // Need this to be able to keep column count
  for (std::vector<OptionDescription>::const_iterator ii = options.opts.begin(), ie = options.opts.end(); ii != ie; ++ii) {
    std::ostringstream header;
    header << " ";
    if (ii->short_name != '\0') {
      header << '-' << ii->short_name;
      if (!ii->long_name.empty())
        header << ',';
    }
    if (!ii->long_name.empty())
      header << "--" << ii->long_name;
    header << "  ";
    std::string header_str = header.str();
    os << header_str;
    
    bool line_empty = false;
    unsigned column = header_str.size();
    for (; column < options_help_indent; ++column)
      os.put(' ');
    
    std::string::const_iterator ji = ii->help.begin(), je = ii->help.end();
    while (ji != je) {
      std::string::const_iterator prev = ji;
      ji = std::find_if(ji, je, (int(*)(int))std::isspace);
      if (std::isspace(*ji))
        ++ji;
      
      unsigned count = ji - prev;
      if (!line_empty && (count + column > options_help_columns)) {
        os << '\n';
        for (column = 0; column != options_help_indent; ++column)
          os.put(' ');
        line_empty = true;
      }
      if (line_empty && std::isspace(*prev)) {
        // Skip block of whitespace after new line
        while ((ji != je) && std::isspace(*ji))
          ++ji;
      } else {
        std::copy(prev, ji, std::ostreambuf_iterator<char>(os.rdbuf()));
        column += count;
      }
    }
    os << '\n';
  }
}

/**
 * \brief Print program options help.
 * 
 * \param argv0 argv[0] as passed to main, to guess the program name.
 * \param description Options description.
 */
void options_help(const char *argv0, const std::string& extra, const OptionsDescription& options) {
  options_help(std::cerr, find_program_name(argv0), extra, options);
}

void options_usage(std::ostream& os, const std::string& program_name, const std::string& extra, const std::string& help_option) {
  os << "Usage: " << program_name << " [options]" << extra << '\n';
  os << "For full help, run " << program_name << ' ' << help_option << "\n";
}

/**
 * \brief Print basic program usage, and how to run help.
 * 
 * \param argv0 argv[0] as passed to main, to guess the program name.
 * \param help_option Full option to use to print full help, usually '-h' or '--help'
 */
void options_usage(const char *argv0, const std::string& extra, const std::string& help_option) {
  options_usage(std::cerr, find_program_name(argv0), extra, help_option);
}

/**
 * \brief OptionDescription constructor.
 * 
 * I have separated this from the class so that the class may still be assigned using initializer
 * syntax in C++98.
 */
OptionDescription option_description(int key, bool has_value, char short_name, const std::string& long_name, const std::string& help) {
  OptionDescription r;
  r.key = key;
  r.has_value = has_value;
  r.short_name = short_name;
  r.long_name = long_name;
  r.help = help;
  return r;
}

OptionParseError::OptionParseError(const std::string& msg)
: std::runtime_error(msg) {
}

OptionParser::OptionParser(const OptionsDescription& options, int argc, const char **argv) {
  m_allow_positional = options.allow_positional;
  m_allow_unknown = options.allow_unknown;
  for (std::vector<OptionDescription>::const_iterator ii = options.opts.begin(), ie = options.opts.end(); ii != ie; ++ii) {
    if (ii->short_name != '\0')
      m_short_options[ii->short_name] = *ii;
    if (!ii->long_name.empty())
      m_long_options[ii->long_name] = *ii;
  }
  
  // Arguments are pushed in reverse order, and argv[0] is ignored
  for (int n = argc - 1; n; --n)
    m_options.push_back(argv[n]);
}

/**
 * \brief Returns true when the entire command line has been parsed.
 */
bool OptionParser::empty() {
  return m_options.empty();
}

/**
 * \brief Parse the next option.
 */
OptionValue OptionParser::next() {
  PSI_ASSERT(!empty());
  
  OptionValue value;
  value.has_value = false;
  value.short_name = '\0';
  
  if (!m_short_option_set.empty())
    return short_option_next();
  
  std::string s = take();
  if ((s.size() >= 2) && (s[0] == '-') && (s[1] == '-')) {
    // Long option
    std::size_t assign_pos = s.find('=', 2);
    value.long_name = s.substr(2, assign_pos == std::string::npos ? std::string::npos : assign_pos-2);
    LongOptionMap::const_iterator ii = m_long_options.find(value.long_name);
    if (value.long_name.empty())
      throw OptionParseError("Empty long option name");
    if (ii != m_long_options.end()) {
      value.key = ii->second.key;
    } else {
      if (!m_allow_unknown)
        throw OptionParseError(boost::str(boost::format("Unknown option '--%s'") % value.long_name));
      value.key = OptionValue::unknown;
    }
    if (assign_pos != std::string::npos) {
      if ((ii != m_long_options.end()) && !ii->second.has_value)
        throw OptionParseError(boost::str(boost::format("Option '--%s' does not expect a value") % value.long_name));
      value.has_value = true;
      value.value = s.substr(assign_pos+1);
    } else {
      if ((ii != m_long_options.end()) && ii->second.has_value) {
        if (empty())
          throw OptionParseError(boost::str(boost::format("Option '--%s' expects a value") % value.long_name));
        value.has_value = true;
        value.value = take();
      }
    }
  } else if ((s.size() >= 1) && (s[0] == '-')) {
    // Short option (or set thereof)
    std::size_t assign_pos = s.find('=', 1);
    m_short_option_set = s.substr(1, assign_pos == std::string::npos ? std::string::npos : assign_pos-1);
    if (m_short_option_set.empty())
      throw OptionParseError("Empty short option set");
    if (assign_pos != std::string::npos) {
      m_short_option_value_set = true;
      m_short_option_value = s.substr(assign_pos+1);
    } else {
      m_short_option_value_set = false;
    }
    return short_option_next();
  } else {
    // Positional option
    if (!m_allow_positional)
      throw OptionParseError("No positional options are accepted");
    value.key = OptionValue::positional;
    value.has_value = true;
    value.value = s;
  }
  
  return value;
}

/**
 * \brief Handle short option processing.
 */
OptionValue OptionParser::short_option_next() {
  PSI_ASSERT(!m_short_option_set.empty());
  
  OptionValue value;
  value.has_value = false;
  value.short_name = m_short_option_set[0];
  m_short_option_set.erase(0,1);
  
  ShortOptionMap::const_iterator ii = m_short_options.find(value.short_name);
  if (ii != m_short_options.end()) {
    value.key = ii->second.key;
  } else {
    if (!m_allow_unknown)
      throw OptionParseError(boost::str(boost::format("Unknown option '-%s'") % value.short_name));
    value.key = OptionValue::unknown;
  }
  
  if (m_short_option_set.empty()) {
    if (m_short_option_value_set) {
      if ((ii != m_short_options.end()) && !ii->second.has_value)
        throw OptionParseError(boost::str(boost::format("Option '-%s' does not expect a value") % value.short_name));
      value.has_value = true;
      value.value = m_short_option_value;
    } else if ((ii != m_short_options.end()) && ii->second.has_value) {
      if (empty())
        throw OptionParseError(boost::str(boost::format("Option '-%s' expects a value") % value.short_name));
      value.has_value = true;
      value.value = take();
    }
  } else {
    if ((ii != m_short_options.end()) && ii->second.has_value)
      throw OptionParseError(boost::str(boost::format("Option '-%s' expects a value") % value.short_name));
  }
  
  return value;
}

/**
 * \brief Pull the next argument from the command line; it is not processed further.
 * 
 * This may be used to implement unspecified options with arguments.
 */
std::string OptionParser::take() {
  std::string s = m_options.back();
  m_options.pop_back();
  return s;
}
}
