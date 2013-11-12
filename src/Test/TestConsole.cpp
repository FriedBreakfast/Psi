#define PSI_TEST_MAIN
#include "Test.hpp"
#include "../Assert.hpp"
#include "../OptionParser.hpp"

#include <iostream>
#include <map>

#ifdef __unix__
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if PSI_HAVE_UCONTEXT
#include <ucontext.h>
#endif

#if PSI_HAVE_EXECINFO
#include <execinfo.h>
#endif
#endif

namespace Psi {
namespace Test {
struct TestRunOptions {
  bool verbose;
  bool fork;
  bool catch_signals;
  unsigned backtrace_depth;
};

enum OptionKeys {
  opt_key_help,
  opt_key_suite_list,
  opt_key_test_list,
  opt_key_run_tests,
  opt_key_verbose,
  opt_key_no_fork,
  opt_key_no_signals
};

/**
  * Parse arguments to test runner.
  * 
  * Note that if a print option was requested, this funtion calls exit() rather than returning.
  */
void run_main_parse_args(int argc, const char **argv, std::vector<std::string>& test_patterns, TestRunOptions& options) {
  OptionsDescription desc;
  desc.allow_unknown = false;
  desc.allow_positional = false;
  desc.opts.push_back(option_description(opt_key_help, false, 'h', "help", "Print this help"));
  desc.opts.push_back(option_description(opt_key_suite_list, false, 's', "", "List test suites in this test program"));
  desc.opts.push_back(option_description(opt_key_test_list, true, 't', "", "List test cases in this test program matching a pattern"));
  desc.opts.push_back(option_description(opt_key_run_tests, true, 'r', "", "Run tests matching a pattern"));
  desc.opts.push_back(option_description(opt_key_verbose, false, 'v', "", "Print all log messages"));
  desc.opts.push_back(option_description(opt_key_no_fork, false, '\0', "no-fork", "Do not run tests in a subprocess"));
  desc.opts.push_back(option_description(opt_key_no_signals, false, '\0', "no-signals", "Do not install signal handlers"));
  
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
      for (const TestSuite *ts = test_suite_list(); ts; ts = ts->next())
        names.push_back(ts->name());
      std::sort(names.begin(), names.end());
      for (std::vector<std::string>::const_iterator ii = names.begin(), ie = names.end(); ii != ie; ++ii)
        std::cerr << *ii << '\n';
      exit(EXIT_SUCCESS);
    }
      
    case opt_key_test_list: {
      std::vector<std::string> names;
      for (const TestSuite *ts = test_suite_list(); ts; ts = ts->next()) {
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
      options.verbose = true;
      break;
      
    case opt_key_no_fork:
      options.fork = false;
      break;
      
    case opt_key_no_signals:
      options.catch_signals = false;
      break;
      
    default: PSI_FAIL("Unexpected option key");
    }
  }
}

bool run_test_case_common(const TestCaseBase *tc, const TestRunOptions& options) {
  std::string name = test_case_name(tc);

  StreamLogger logger(&std::cerr, name, options.verbose ? log_level_all : log_level_fail);
  set_test_logger(&logger);
  try {
    tc->run();
  } catch (std::exception& ex) {
    logger.except(ex.what());
  } catch (...) {
    logger.except("Unknown exception raised");
  }
  
  return logger.passed();
}

#ifdef __unix__
#if PSI_HAVE_EXECINFO && PSI_HAVE_UCONTEXT
namespace {
bool signal_exiting;
ucontext_t signal_exit_context;

void signal_handler(int PSI_UNUSED(signum), siginfo_t *PSI_UNUSED(info), void *PSI_UNUSED(ptr)) {
  void *backtrace_buffer[10];
  int n = backtrace(backtrace_buffer, 10);
  backtrace_symbols_fd(backtrace_buffer, n, 2);
  
  signal_exiting = true;
  if (setcontext(&signal_exit_context) != 0) {
    perror("Failed to jump out of signal handler");
    _exit(EXIT_FAILURE);
  }
}
}
#endif

bool run_test_case_signals(const TestCaseBase *tc, const TestRunOptions& options) {
#if PSI_HAVE_EXECINFO && PSI_HAVE_UCONTEXT
  if (options.catch_signals) {
    signal_exiting = false;
    if (getcontext(&signal_exit_context) != 0) {
      perror("Failed to save signal handler exit context");
      return false;
    }
    
    if (signal_exiting) {
      return false;
    }
    
    stack_t signal_stack, old_signal_stack;
    signal_stack.ss_flags = 0;
    signal_stack.ss_size = SIGSTKSZ;
    signal_stack.ss_sp = std::malloc(signal_stack.ss_size);
    if (sigaltstack(&signal_stack, &old_signal_stack) != 0) {
      perror("Failed to establish signal stack");
      return false;
    }
    
    const unsigned n_signals = 7;
    int caught_signals[n_signals] = {SIGFPE, SIGSEGV, SIGTERM, SIGILL, SIGSYS, SIGBUS, SIGABRT};
    struct sigaction old_actions[n_signals];
    struct sigaction new_action;
    new_action.sa_sigaction = signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (unsigned i = 0; i != n_signals; ++i) {
      if (sigaction(caught_signals[i], &new_action, &old_actions[i]) != 0) {
        perror("Failed to set signal handler");
        return false;
      }
    }
    
    bool success = run_test_case_common(tc, options);
    
    for (unsigned i = 0; i != n_signals; ++i) {
      sigaction(caught_signals[i], &old_actions[i], NULL);
    }
    
    sigaltstack(&old_signal_stack, NULL);
    
    return success;
  }
#endif

  return run_test_case_common(tc, options);
}

bool run_test_case(const TestCaseBase *tc, const TestRunOptions& options) {
  if (options.fork) {
    std::cout.flush();
    std::cerr.flush();
    pid_t child_pid = fork();
    if (child_pid == 0) {
      bool good = run_test_case_signals(tc, options);
      std::cout.flush();
      std::cerr.flush();
      _exit(good ? EXIT_SUCCESS : EXIT_FAILURE);
    } else {
      int child_status;
      waitpid(child_pid, &child_status, 0);
      if (WIFEXITED(child_status)) {
        return WEXITSTATUS(child_status) == EXIT_SUCCESS;
      } else if (WIFSIGNALED(child_status)) {
        std::cerr << "Child exited due to signal: " << strsignal(WTERMSIG(child_status)) << std::endl;
        return false;
      } else {
        std::cerr << "Child exited for unknown reason" << std::endl;
        return false;
      }
    }
  } else {
    return run_test_case_signals(tc, options);
  }
}
#else
bool run_test_case(const TestCaseBase *tc, const TestRunOptions& options) {
  return run_test_case_common(tc, options);
}
#endif
    
int run_main(int argc, const char** argv) {
  TestRunOptions options;
  options.verbose = false;
  options.fork = true;
  options.catch_signals = true;
  options.backtrace_depth = 5;
  
  std::vector<std::string> test_patterns;
  run_main_parse_args(argc, argv, test_patterns, options);
  
  std::map<std::string, const TestCaseBase*> test_cases;

  for (const TestSuite *ts = test_suite_list(); ts; ts = ts->next()) {
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
    if (options.verbose)
      std::cerr << "Starting test " << ii->first << '\n';
    
    if (!run_test_case(ii->second, options)) {
      std::cerr << "Test failed: " << ii->first << '\n';
      failures++;
    }
  }
  
  std::cerr << test_cases.size() << " tests run, " << failures << " failures\n";
  
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
}
}
