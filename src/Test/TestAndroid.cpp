#include "Test.hpp"
#include <jni.h>

extern "C" JNIEXPORT void JNICALL Java_zeta_psi_tests_TestLoader_buildPsiTests(JNIEnv *env, jclass cls, jobject parent_suite) {
  jmethodID callback = env->GetStaticMethodID(cls, "addPsiTest", "(Ljunit/framework/TestSuite;Ljava/lang/String;J)V");
  if (!callback)
    return;
  
  jclass testsuite_cls = env->FindClass("junit/framework/TestSuite");
  if (!testsuite_cls)
    return;
  jmethodID testsuite_ctor = env->GetMethodID(testsuite_cls, "<init>", "(Ljava/lang/String;)V");
  if (!testsuite_ctor)
    return;
  jmethodID testsuite_add = env->GetMethodID(testsuite_cls, "addTest", "(Ljunit/framework/Test;)V");
  
  for (const Psi::Test::TestSuite *psi_suite = Psi::Test::test_suite_list(); psi_suite; psi_suite = psi_suite->next()) {
    jobject suite_name = env->NewStringUTF(psi_suite->name());
    if (!suite_name)
      return;
    
    jobject junit_suite = env->NewObject(testsuite_cls, testsuite_ctor, suite_name);
    env->DeleteLocalRef(suite_name);
    
    for (const Psi::Test::TestCaseBase *tc = psi_suite->test_cases(); tc; tc = tc->next()) {
      jobject test_name = env->NewStringUTF(tc->name());
      if (!test_name)
        return;
      
      env->CallStaticVoidMethod(cls, callback, junit_suite, test_name, reinterpret_cast<jlong>(tc));
      if (env->ExceptionCheck())
        return;
      
      env->DeleteLocalRef(test_name);
    }
    
    env->CallVoidMethod(parent_suite, testsuite_add, junit_suite);
    
    env->DeleteLocalRef(junit_suite);
  }
}

namespace {
  class TestExitException {};
  
  class JNILogger : public Psi::Test::TestLogger {
    JNIEnv *m_env;
    jclass m_junit_assert;
    jmethodID m_junit_assert_true;
    jmethodID m_junit_fail;
    bool m_good;
    Psi::Test::TestLocation m_last_location;
    
  public:
    JNILogger(JNIEnv *env, jclass junit_assert, jmethodID junit_assert_true, jmethodID junit_fail)
    : m_env(env),
    m_junit_assert(junit_assert), m_junit_assert_true(junit_assert_true), m_junit_fail(junit_fail),
    m_good(true), m_last_location(NULL, 0) {}
    
    virtual bool passed() {
      return !m_good;
    }
    
    virtual void message(const Psi::Test::TestLocation& loc, const std::string& str) {
      m_last_location = loc;

      std::ostringstream ss;
      ss << loc.file << ':' << loc.line << ": " << str << std::endl;
      jobject message = m_env->NewStringUTF(ss.str().c_str());
      m_env->CallStaticVoidMethod(m_junit_assert, m_junit_assert_true, message, (jboolean)true);
      m_env->DeleteLocalRef(message);
    }
    
    virtual void check(const Psi::Test::TestLocation& loc, Psi::Test::Level, bool passed, const std::string& cond_str, const std::string& cond_fmt) {
      m_last_location = loc;
      m_good = m_good && passed;
      
      std::ostringstream ss;
      ss << loc.file << ':' << loc.line << ": " << cond_str;
      if (!cond_fmt.empty())
        ss << " [" << cond_fmt << ']';
      
      jobject message = m_env->NewStringUTF(ss.str().c_str());
      m_env->CallStaticVoidMethod(m_junit_assert, m_junit_assert_true, message, (jboolean)passed);
      m_env->DeleteLocalRef(message);
      if (!passed)
        throw TestExitException();
    }
    
    virtual void except(const std::string& what) {
      std::ostringstream ss;
      m_good = false;
      ss << "Exception occurred: " << what << '\n';
      if (m_last_location.file)
        ss << "Last location was: " << m_last_location.file << ':' << m_last_location.line << '\n';
      else
        ss << "No checks have been performed so no previous location is available\n";
      
      jobject message = m_env->NewStringUTF(ss.str().c_str());
      m_env->CallStaticVoidMethod(m_junit_assert, m_junit_fail, message);
      m_env->DeleteLocalRef(message);
      throw TestExitException();
    }
  };
}

extern "C" JNIEXPORT void JNICALL Java_zeta_psi_tests_TestLoader_runPsiTest(JNIEnv *env, jclass, jlong address) {
  const Psi::Test::TestCaseBase *tc = reinterpret_cast<const Psi::Test::TestCaseBase*>(address);

  jclass junit_assert = env->FindClass("junit/framework/Assert");
  if (!junit_assert)
    return;
  jmethodID junit_assert_true = env->GetStaticMethodID(junit_assert, "assertTrue", "(Ljava/lang/String;Z)V");
  if (!junit_assert_true)
    return;
  jmethodID junit_fail = env->GetStaticMethodID(junit_assert, "fail", "(Ljava/lang/String;)V");
  if (!junit_fail)
    return;
  
  try {
    JNILogger logger(env, junit_assert, junit_assert_true, junit_fail);
    Psi::Test::set_test_logger(&logger);
    
    try {
      tc->run();
    } catch (TestExitException&) {
      if (!env->ExceptionCheck())
        logger.except("Test exited by throwing a failure exception, but no Java exception has been raised");
    } catch (std::exception& ex) {
      logger.except(ex.what());
    } catch (...) {
      logger.except("Unknown exception raised");
    }
  } catch (...) {
    return;
  }
}
