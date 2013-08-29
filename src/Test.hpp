#ifndef HPP_PSI_TEST
#define HPP_PSI_TEST

#include <sstream>
#include <stdexcept>

#include "Export.hpp"

/**
 * This is basically a rip-off of Boost.Test, but I'm bloody tired of spending my days
 * fucking around trying to get Boost and CMake configurations right to make Windows
 * builds work.
 */

namespace Psi {
  namespace Test {
    enum LogLevel {
      log_level_all,
      log_level_fail
    };
    
    enum Level {
      level_check,
      level_require
    };
    
    struct TestLocation {
      const char *file;
      int line;
      
      TestLocation(const char *file_, int line_) : file(file_), line(line_) {}
    };

    class TestLogger {
    public:
      virtual bool passed() = 0;
      virtual void message(const TestLocation& loc, const std::string& str) = 0;
      virtual void check(const TestLocation& loc, Level level, bool passed, const std::string& cond_str, const std::string& cond_fmt) = 0;
      virtual void except(const std::string& what) = 0;
    };
    
    class TestCaseBase;
    
    class PSI_TEST_EXPORT TestSuite {
      friend class TestCaseBase;
      
      const char *m_name;
      TestCaseBase *m_test_cases;
      const TestSuite *m_next;
      
    public:
      TestSuite(const char *name);
      
      const char *name() const {return m_name;}
      const TestCaseBase *test_cases() const {return m_test_cases;}
      const TestSuite* next() const {return m_next;}
    };
    
    class EmptySuiteFixture {};

    class PSI_TEST_EXPORT TestCaseBase {
      const TestSuite *m_suite;
      const char *m_name;
      const TestCaseBase *m_next;
      
    public:
      TestCaseBase(TestSuite& suite, const char *name);
      virtual void run() const = 0;
      const TestSuite *suite() const {return m_suite;}
      const char *name() const {return m_name;}
      const TestCaseBase *next() const {return m_next;}
    };
    
    class EmptyCaseFixture;
    
    class RequiredCheckFailed : public std::exception {
    public:
      RequiredCheckFailed();
      virtual ~RequiredCheckFailed() throw();
      virtual const char *what() const throw();
    };
    
    PSI_TEST_EXPORT void check_condition(const TestLocation& loc, Level level, bool passed, const std::string& cond_str, const std::string& cond_fmt);
    
    template<typename T, typename U>
    void check_equal(const TestLocation& loc, Level level, const T& x, const U& y, const char *s) {
      std::stringstream ss;
      ss << x << " == " << y;
      check_condition(loc, level, x==y, s, ss.str());
    }
    
    template<typename T, typename U>
    void check_equal_range(const TestLocation& loc, Level level, T x1, T x2, U y1, U y2, const char *s) {
      bool same = true;
      T x = x1;
      U y = y1;
      while (true) {
        if ((x == x2) || (y == y2)) {
          same = (x == x2) && (y == y2);
          break;
        } else if (*x != *y) {
          same = false;
          break;
        }
        
        ++x; ++y;
      }
      
      std::stringstream ss;
      ss << '{';
      for (x = x1; x != x2; ++x) {
        if (x != x1) ss << ',';
        ss << *x;
      }
      ss << "} == {";
      for (y = y1; y != y2; ++y) {
        if (y != y1) ss << ',';
        ss << *y;
      }
      ss << '}';
      check_condition(loc, level, same, s, ss.str());
    }

    PSI_TEST_EXPORT int run_main(int argc, const char **argv);
  }
}

#define PSI_TEST_SUITE_FIXTURE(name,fix) \
  namespace { \
    typedef fix PsiTest_SuiteFixtureType; \
    ::Psi::Test::TestSuite PsiTest_Suite(#name);

#define PSI_TEST_SUITE_END() }
#define PSI_TEST_SUITE(name) PSI_TEST_SUITE_FIXTURE(name,::Psi::Test::EmptySuiteFixture)

#define PSI_TEST_CASE_FIXTURE(name,fix) \
  struct name : public fix { \
    void test_main(); \
  }; \
  \
  class PsiTestRunner_ ## name : public ::Psi::Test::TestCaseBase { \
  public: \
    PsiTestRunner_ ## name() : ::Psi::Test::TestCaseBase(PsiTest_Suite, #name) {} \
    \
    virtual void run() const { \
      name test; \
      test.test_main(); \
    } \
  } PsiTestRunnerInst_ ## name; \
  \
  void name::test_main()

#define PSI_TEST_CASE(name) PSI_TEST_CASE_FIXTURE(name,PsiTest_SuiteFixtureType)

#define PSI_TEST_LOCATION ::Psi::Test::TestLocation(__FILE__,__LINE__)

#define PSI_TEST_CHECK(a) ::Psi::Test::check_condition(PSI_TEST_LOCATION,::Psi::Test::level_check,(a),#a,"")
#define PSI_TEST_REQUIRE(a) ::Psi::Test::check_condition(PSI_TEST_LOCATION,::Psi::Test::level_require,(a),#a,"")

#define PSI_TEST_EQUAL(l,a,b) ::Psi::Test::check_equal(PSI_TEST_LOCATION, l, a, b, #a " == " #b)
#define PSI_TEST_CHECK_EQUAL(a,b) PSI_TEST_EQUAL(::Psi::Test::level_check,a,b)
#define PSI_TEST_REQUIRE_EQUAL(a,b) PSI_TEST_EQUAL(::Psi::Test::level_require,a,b)

#define PSI_TEST_EQUAL_RANGE(l,a1,a2,b1,b2) ::Psi::Test::check_equal_range(PSI_TEST_LOCATION, l, a1, a2, b1, b2, "["#a1","#a2"]==["#b1","#b2"]")
#define PSI_TEST_CHECK_EQUAL_RANGE(a1,a2,b1,b2) PSI_TEST_EQUAL_RANGE(::Psi::Test::level_check,a1,a2,b1,b2)
#define PSI_TEST_REQUIRE_EQUAL_RANGE(a1,a2,b1,b2) PSI_TEST_EQUAL_RANGE(::Psi::Test::level_require,a1,a2,b1,b2)

#define PSI_TEST_MESSAGE(a) psi_test_suite_logger.message((::Psi::Test::FormatInserter() fmt).str())

#endif
