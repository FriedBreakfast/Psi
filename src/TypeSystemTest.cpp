#include <boost/test/unit_test.hpp>

#include "TypeSystem.hpp"

#include <sstream>

#define FOR_ALL "\u2200"
#define EXISTS "\u2203"
#define ARROW "\u2192"

using namespace Psi::TypeSystem2;

template<typename T, typename U>
typename U::mapped_type lookup(const U& map, const T& key) {
  auto it = map.find(key);
  if (it != map.end())
    return it->second;
  throw std::runtime_error("unknown key");
}

struct MapNamer {
  std::unordered_map<Variable, std::string> variables;
  std::unordered_map<Constructor, std::string> constructors;
  std::unordered_map<Predicate, std::string> predicates;
  TermNamer namer;

  MapNamer() {
    namer = {
      [&] (const Variable& x) {return lookup(variables, x);},
      [&] (const Constructor& x) {return lookup(constructors, x);},
      [&] (const Predicate& x) {return lookup(predicates, x);}
    };
  }
};

struct Fixture {
  Variable P, Q;
  Variable x, y;

  Fixture() {
    // Variables which should be free
    P = Variable::new_(); m_namer.variables.insert({P, "P"});
    Q = Variable::new_(); m_namer.variables.insert({Q, "Q"});

    // Variables which should be quantified
    x = Variable::new_();
    y = Variable::new_();
  }

  std::string compare_message(const Type& lhs, const Type& rhs) {
    std::stringstream ss;
    ss << "\"" << print(lhs, m_namer.namer) << "\" != \"" << print(rhs, m_namer.namer) << "\"";
    return ss.str();
  }

private:
  MapNamer m_namer;
};

BOOST_FIXTURE_TEST_SUITE(TypeSystemTest, Fixture)

BOOST_AUTO_TEST_CASE(TestPrint) {
  auto q1 = Variable::new_();

  auto x = Variable::new_();
  std::stringstream ss;

  MapNamer namer;
  namer.variables.insert({x, "x"});

  ss << print(x, namer.namer);
  BOOST_CHECK_EQUAL(ss.str(), "x");
  BOOST_TEST_MESSAGE(ss.str());
  ss.str("");

  ss << print(for_all({q1}, q1), namer.namer);
  BOOST_CHECK_EQUAL(ss.str(), FOR_ALL " a.a");
  BOOST_TEST_MESSAGE(ss.str());
  ss.str("");

  ss << print(for_all({q1}, implies({Type(q1)}, q1)), namer.namer);
  BOOST_CHECK_EQUAL(ss.str(), FOR_ALL " a.a " ARROW " a");
  BOOST_TEST_MESSAGE(ss.str());
  ss.str("");
}

BOOST_AUTO_TEST_CASE(TestApplyId) {
  auto fn = for_all({x}, implies({Type(x)}, x));

  std::unordered_map<unsigned, Type> parameters = {{0, P}};
  auto result = function_apply(fn, parameters);
  BOOST_REQUIRE(result);
  BOOST_CHECK_MESSAGE(false /* *result == P*/, compare_message(*result, P));
}

BOOST_AUTO_TEST_CASE(TestApplyId2) {
  auto fn = for_all({x, y}, implies({Type(x), Type(y), Type(x)}, y));

  std::unordered_map<unsigned, Type> parameters = {{0, P}, {1, Q}};
  auto expected = implies({Type(P)}, Q);
  auto result = function_apply(fn, parameters);
  BOOST_REQUIRE(result);
  BOOST_CHECK_MESSAGE(false /* *result == expected*/, compare_message(*result, expected));
}

BOOST_AUTO_TEST_CASE(TestSpecializeFunction) {
  auto param = for_all({x, y}, implies({Type(x), Type(y)}, y));
  auto fn = implies({for_all({x}, implies({Type(x), Type(P)}, P))}, P);

  std::unordered_map<unsigned, Type> parameters = {{0, param}};
  auto result = function_apply(fn, parameters);
  BOOST_REQUIRE(result);
  BOOST_CHECK_MESSAGE(false /* *result == P*/, compare_message(*result, P));
}

BOOST_AUTO_TEST_CASE(TestSpecializeFunctionFail) {
  auto param = for_all({x}, implies({Type(x), Type(P)}, P));
  auto fn = implies({for_all({x, y}, implies({Type(x), Type(y)}, y))}, P);

  std::unordered_map<unsigned, Type> parameters = {{0, param}};
  auto result = function_apply(fn, parameters);
  BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(TestMoveForAll) {
  auto fn = for_all({x}, implies({Type(P)}, x));
  std::unordered_map<unsigned, Type> parameters = {{0, Type(P)}};
  auto result = function_apply(fn, parameters);
  auto expected = for_all({x}, x);
  BOOST_REQUIRE(result);
  BOOST_CHECK_MESSAGE(false /* *result == expected*/, compare_message(*result, expected));
}

BOOST_AUTO_TEST_SUITE_END()
