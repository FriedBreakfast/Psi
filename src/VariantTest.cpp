#include <boost/test/unit_test.hpp>

#include "Variant.hpp"
#include <string>

BOOST_AUTO_TEST_SUITE(VariantTest)

BOOST_AUTO_TEST_CASE(TestVisit) {
  Psi::Variant<char, const char*> t;

  auto empty = [](Psi::None) {return 0;};
  auto one = [](const char *) {return 1;};
  auto two = [](char) {return 2;};

  t = static_cast<const char*>("Hello World\n");
  BOOST_CHECK_EQUAL(t.visit(one, two, empty), 1);

  t = 'x';
  BOOST_CHECK_EQUAL(t.visit(one, two, empty), 2);

  t.clear();
  BOOST_CHECK(t.empty());
  BOOST_CHECK_EQUAL(t.visit(empty, one, two), 0);
}

BOOST_AUTO_TEST_CASE(TestConstAnonymous) {
  /*
   * Test using anonymous visitors on constant variants.
   */
  const Psi::Variant<char, std::string> t(std::string("Hello World"));
  BOOST_CHECK_EQUAL(t.visit2([] (char) {return 0;}, [] (const std::string&) {return 1;}), 1);

  const Psi::Variant<char, std::string> u('X');
  BOOST_CHECK_EQUAL(u.visit2([] (char) {return 0;}, [] (const std::string&) {return 1;}), 0);
}

BOOST_AUTO_TEST_CASE(AssignConstruct) {
  /*
   * For some reason the version below fails to compile:
   *
   * const Psi::Variant<char, std::string> t = std::string("Hello World");
   */
  const Psi::Variant<char, std::string> t(std::string("Hello World"));
  const Psi::Variant<char, std::string> u = t;
  const Psi::Variant<char, std::string> v(u);
}

BOOST_AUTO_TEST_CASE(DefaultVisit) {
  const Psi::Variant<char, std::string> t(std::string("Hello World"));
  BOOST_CHECK_EQUAL(t.visit_default(27), 27);
  BOOST_CHECK_EQUAL(t.visit_default(5, [](const std::string&) {return 9;}), 9);
}

BOOST_AUTO_TEST_SUITE_END()
