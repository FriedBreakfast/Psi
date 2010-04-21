#include <boost/test/unit_test.hpp>

#include <utility>

#include "Maybe.hpp"

BOOST_AUTO_TEST_SUITE(MaybeTest)

BOOST_AUTO_TEST_CASE(OrderTest) {
  Psi::Maybe<int> x, y;

  BOOST_CHECK(x.empty());
  BOOST_CHECK(y.empty());

  x = 4;
  BOOST_REQUIRE(!x.empty());
  BOOST_CHECK(*x == 4);
  BOOST_CHECK(x == 4);
  BOOST_CHECK(4 == x);
  BOOST_CHECK(x != 5);
  BOOST_CHECK(5 != x);
  BOOST_CHECK(y < x);
  BOOST_CHECK(y <= x);
  BOOST_CHECK(x > y);
  BOOST_CHECK(x >= y);
  BOOST_CHECK(y != x);

  y = 2;
  BOOST_CHECK(!y.empty());
  BOOST_CHECK(y < x);
  BOOST_CHECK(y <= x);
  BOOST_CHECK(x > y);
  BOOST_CHECK(x >= y);
  BOOST_CHECK(y != x);

  y = 4;
  BOOST_CHECK(y == x);
  BOOST_CHECK(y <= x);
  BOOST_CHECK(x <= y);

  x.clear();
  y.clear();
  BOOST_CHECK(x.empty());
  BOOST_CHECK(y.empty());
  BOOST_CHECK(x == y);
  BOOST_CHECK(y <= x);
  BOOST_CHECK(x <= y);
}

BOOST_AUTO_TEST_CASE(ConstructorTest) {
  Psi::Maybe<std::pair<int, std::string> > x = {3, "Hello World"};

  BOOST_REQUIRE(!x.empty());
  BOOST_CHECK(x->first == 3);
  BOOST_CHECK(x->second == "Hello World");
}

BOOST_AUTO_TEST_SUITE_END()
