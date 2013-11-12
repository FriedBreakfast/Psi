#include "../Assert.hpp"
#include "Test.hpp"

namespace Psi {
namespace Test {
namespace {
  // This will initialize before constructors because it is a constant
  const TestSuite *global_suite_list = NULL;
}

/// \brief Return the global of detected test suites
const TestSuite* test_suite_list() {
  return global_suite_list;
}

RequiredCheckFailed::RequiredCheckFailed() {
}

RequiredCheckFailed::~RequiredCheckFailed() throw() {
}

const char* RequiredCheckFailed::what() const throw() {
  return "psi-test required check failed";
}

TestSuite::TestSuite(const char* name)
: m_name(name), m_test_cases(NULL) {
  m_next = global_suite_list;
  global_suite_list = this;
}

TestCaseBase::TestCaseBase(TestSuite& suite, const char* name)
: m_suite(&suite), m_name(name) {
  // Append to list
  m_next = suite.m_test_cases;
  suite.m_test_cases = this;
}

TestCaseBase::~TestCaseBase() {
}

std::string test_case_name(const TestCaseBase *tc) {
  std::stringstream ss;
  ss << tc->suite()->name() << '.' << tc->name();
  return ss.str();
}

namespace {
  TestLogger *current_logger = NULL;
}

void set_test_logger(TestLogger *logger) {
  current_logger = logger;
}

void check_condition(const TestLocation& loc, Level level, bool passed, const std::string& cond_str, const std::string& cond_fmt) {
  current_logger->check(loc, level, passed, cond_str, cond_fmt);
  if (!passed) {
    if (level == level_require)
      throw RequiredCheckFailed();
  }
}

bool glob_partial(const std::string& s, const std::string& p, std::size_t s_idx, std::size_t p_idx, std::size_t p_rem) {
  std::size_t s_n = s.size(), p_n = p.size();
  while (p_idx < p_n) {
    if ((p[p_idx] == '*') || (p[p_idx] == '?')) {
      // Merge '*' and '?' occurences
      while (p_idx < p_n) {
        if (p[p_idx] == '?') {
          ++p_idx;
          --p_rem;
          if (s_idx == s_n)
            return false;
          ++s_idx;
        } else if (p[p_idx] == '*') {
          ++p_idx;
        } else {
          break;
        }
      }
      
      PSI_ASSERT(p_rem + s_idx <= s_n);
      std::size_t nmax = s_n - s_idx - p_rem;
      for (std::size_t offset = 0; offset < nmax; ++offset) {
        if (glob_partial(s, p, s_idx + offset, p_idx, p_rem))
          return true;
      }
      
      return false;
    } else {
      if ((s_idx == s_n) || (s[s_idx] != p[p_idx]))
        return false;
      ++s_idx;
      ++p_idx;
      --p_rem;
    }
  }
  
  return true;
}

/**
  * Check whether a string matches a wildcard pattern
  */
bool glob(const std::string& s, const std::string& pattern) {
  std::size_t char_count = 0;
  for (std::string::const_iterator ii = pattern.begin(), ie = pattern.end(); ii != ie; ++ii) {
    if (*ii != '*')
      ++char_count;
  }
  
  if (char_count > s.size())
    return false;
  
  return glob_partial(s, pattern, 0, 0, char_count);
}

StreamLogger::StreamLogger(std::ostream *os_, const std::string& name_, LogLevel print_level_)
: os(os_), name(name_), error_count(0), print_level(print_level_), last_location(NULL, 0) {
}

bool StreamLogger::passed() {
  return !error_count;
}

void StreamLogger::message(const TestLocation& loc, const std::string& str) {
  last_location = loc;
  *os << loc.file << ':' << loc.line << ": " << str << std::endl;
}

void StreamLogger::check(const TestLocation& loc, Level, bool passed, const std::string& cond_str, const std::string& cond_fmt) {
  last_location = loc;
  
  bool print = false;
  const char *state = "";
  if (!passed) {
    print = true;
    state = "failed";
    ++error_count;
  } else if (print_level == log_level_all) {
    print = true;
    state = "passed";
  }
  
  if (print) {
    *os << loc.file << ':' << loc.line << ": check " << state << ": " << cond_str;
    if (!cond_fmt.empty())
      *os << " [" << cond_fmt << ']';
    *os << std::endl;
  }
}

void StreamLogger::except(const std::string& what) {
  ++error_count;
  *os << "Exception occurred: " << what << '\n';
  if (last_location.file)
    *os << "Last location was: " << last_location.file << ':' << last_location.line << '\n';
  else
    *os << "No checks have been performed so no previous location is available\n";
  *os << std::flush;
}
}
}
