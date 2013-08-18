#include "Assert.hpp"
#include "Test.hpp"
#include "OptionParser.hpp"

#include <iostream>
#include <map>
#include <vector>

#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Psi {
namespace Test {
namespace {
  // This will initialize before constructors because it is a constant
  const TestSuite *global_suite_list = NULL;
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

std::string test_case_name(const TestCaseBase *tc) {
  std::stringstream ss;
  ss << tc->suite()->name() << '.' << tc->name();
  return ss.str();
}

namespace {
  TestLogger *current_logger = NULL;
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

enum OptionKeys {
  opt_key_help,
  opt_key_suite_list,
  opt_key_test_list,
  opt_key_run_tests,
  opt_key_verbose
};

/**
  * Parse arguments to test runner.
  * 
  * Note that if a print option was requested, this funtion calls exit() rather than returning.
  */
void run_main_parse_args(int argc, const char **argv, std::vector<std::string>& test_patterns, bool& verbose) {
  OptionsDescription desc;
  desc.allow_unknown = false;
  desc.allow_positional = false;
  desc.opts.push_back(option_description(opt_key_help, false, 'h', "help", "Print this help"));
  desc.opts.push_back(option_description(opt_key_suite_list, false, 's', "", "List test suites in this test program"));
  desc.opts.push_back(option_description(opt_key_test_list, true, 't', "", "List test cases in this test program matching a pattern"));
  desc.opts.push_back(option_description(opt_key_run_tests, true, 'r', "", "Run tests matching a pattern"));
  desc.opts.push_back(option_description(opt_key_verbose, false, 'v', "", "Print all log messages"));
  
  Psi::OptionParser parser(desc, argc, argv);
  while (!parser.empty()) {
    Psi::OptionValue val;
    try {
      val = parser.next();
    } catch (Psi::OptionParseError& ex) {
      std::cerr << ex.what() << '\n';
      Psi::options_usage(argv[0], "", "-h");
      exit(EXIT_FAILURE);
    }
    
    switch (val.key) {
    case opt_key_help:
      Psi::options_help(argv[0], "", desc);
      exit(EXIT_SUCCESS);

    case opt_key_suite_list: {
      std::vector<std::string> names;
      for (const TestSuite *ts = global_suite_list; ts; ts = ts->next())
        names.push_back(ts->name());
      std::sort(names.begin(), names.end());
      for (std::vector<std::string>::const_iterator ii = names.begin(), ie = names.end(); ii != ie; ++ii)
        std::cerr << *ii << '\n';
      exit(EXIT_SUCCESS);
    }
      
    case opt_key_test_list: {
      std::vector<std::string> names;
      for (const TestSuite *ts = global_suite_list; ts; ts = ts->next()) {
        for (const TestCaseBase *tc = ts->test_cases(); tc; tc = tc->next()) {
          std::string name = test_case_name(tc);
          if (glob(name, val.value))
            names.push_back(name);
        }
      }
      std::sort(names.begin(), names.end());
      for (std::vector<std::string>::const_iterator ii = names.begin(), ie = names.end(); ii != ie; ++ii)
        std::cerr << *ii << '\n';
      exit(EXIT_SUCCESS);
    }
    
    case opt_key_run_tests:
      test_patterns.push_back(val.value);
      break;
    
    case opt_key_verbose:
      verbose = true;
      break;
      
    default: PSI_FAIL("Unexpected option key");
    }
  }
}

class StreamLogger : public TestLogger {
  std::ostream *os;
  std::string name;
  unsigned error_count;
  LogLevel print_level;
  
public:
  StreamLogger(std::ostream *os_, const std::string& name_, LogLevel print_level_)
  : os(os_), name(name_), error_count(0), print_level(print_level_) {}
  
  virtual bool passed() {return !error_count;}
  
  virtual void message(const TestLocation& loc, const std::string& str) {
    *os << loc.file << ':' << loc.line << ": " << str << '\n';
  }
  
  virtual void check(const TestLocation& loc, Level, bool passed, const std::string& cond_str, const std::string& cond_fmt) {
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
      *os << '\n';
    }
  }
};

#if __linux__
bool run_test_case(const TestCaseBase *tc, LogLevel level) {
  std::string name = test_case_name(tc);
  
  std::cout.flush();
  std::cerr.flush();
  pid_t child_pid = fork();
  if (child_pid == 0) {
    StreamLogger logger(&std::cerr, name, level);
    current_logger = &logger;
    tc->run();
    _exit(logger.passed() ? EXIT_SUCCESS : EXIT_FAILURE);
  } else {
    int child_status;
    waitpid(child_pid, &child_status, 0);
    return WIFEXITED(child_status) && (WEXITSTATUS(child_status) == EXIT_SUCCESS);
  }
}
#else
bool run_test_case(const TestCaseBase *tc, LogLevel level) {
  std::string name = test_case_name(tc);
  
  StreamLogger logger(&std::cerr, name, level);
  current_logger = &logger;
  tc->run();
  return logger.passed();
}
#endif
    
int run_main(int argc, const char** argv) {
  bool verbose = false;
  std::vector<std::string> test_patterns;
  run_main_parse_args(argc, argv, test_patterns, verbose);
  
  std::map<std::string, const TestCaseBase*> test_cases;

  for (const TestSuite *ts = global_suite_list; ts; ts = ts->next()) {
    for (const TestCaseBase *tc = ts->test_cases(); tc; tc = tc->next()) {
      std::string name = test_case_name(tc);
      if (test_patterns.empty()) {
        test_cases.insert(std::make_pair(name, tc));
      } else {
        for (std::vector<std::string>::const_iterator ii = test_patterns.begin(), ie = test_patterns.end(); ii != ie; ++ii) {
          if (glob(name, *ii)) {
            test_cases.insert(std::make_pair(name, tc));
            break;
          }
        }
      }
    }
  }
  
  std::cerr << "Running " << test_cases.size() << " tests...\n";
  
  unsigned failures = 0;
  for (std::map<std::string, const TestCaseBase*>::const_iterator ii = test_cases.begin(), ie = test_cases.end(); ii != ie; ++ii) {
    if (verbose)
      std::cerr << "Starting test " << ii->first << '\n';
    if (!run_test_case(ii->second, verbose ? log_level_all : log_level_fail)) {
      std::cerr << "Test failed: " << ii->first << '\n';
      failures++;
    }
  }
  
  std::cerr << test_cases.size() << " tests run, " << failures << " failures\n";
  
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
}
}
