#include <boost/test/unit_test.hpp>

#include "Variant.hpp"

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

BOOST_AUTO_TEST_SUITE_END()
