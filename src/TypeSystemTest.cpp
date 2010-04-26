#include <boost/test/unit_test.hpp>

#include "TypeSystem.hpp"

#include <sstream>

#define FOR_ALL "\u2200"
#define EXISTS "\u2203"
#define ARROW "\u2192"

BOOST_AUTO_TEST_SUITE(TypeSystemTest)

using namespace Psi::TypeSystem2;

template<typename T, typename U>
typename U::mapped_type lookup(const U& map, const T& key) {
  auto it = map.find(key);
  if (it != map.end())
    return it->second;
  throw std::runtime_error("unknown key");
}

BOOST_AUTO_TEST_CASE(TestPrint) {
  auto q1 = Variable::new_();

  auto x = Variable::new_();
  std::stringstream ss;

  std::unordered_map<Variable, std::string> var_names;
  var_names.insert({x, "x"});

  TermNamer namer = {
    [&] (const Variable& var) {return lookup(var_names, var);},
    [] (const Constructor&) -> std::string {throw std::runtime_error("");},
    [] (const Predicate&) -> std::string {throw std::runtime_error("");}
  };

  ss << print(x, namer);
  BOOST_CHECK_EQUAL(ss.str(), "x");
  BOOST_TEST_MESSAGE(ss.str());
  ss.str("");

  ss << print(for_all({q1}, q1), namer);
  BOOST_CHECK_EQUAL(ss.str(), FOR_ALL " a.a");
  BOOST_TEST_MESSAGE(ss.str());
  ss.str("");

  ss << print(for_all({q1}, implies({Type(q1)}, q1)), namer);
  BOOST_CHECK_EQUAL(ss.str(), FOR_ALL " a.a " ARROW " a");
  BOOST_TEST_MESSAGE(ss.str());
  ss.str("");
}

BOOST_AUTO_TEST_CASE(TestApplyId) {
  auto x = Variable::new_();

  std::unordered_map<Variable, std::string> var_names;
  var_names.insert({x, "x"});

  TermNamer namer = {
    [&] (const Variable& var) {return lookup(var_names, var);},
    [] (const Constructor&) -> std::string {throw std::runtime_error("");},
    [] (const Predicate&) -> std::string {throw std::runtime_error("");}
  };

  auto T = Variable::new_();
  auto fn = for_all({x}, implies({Type(x)}, x));
}

BOOST_AUTO_TEST_SUITE_END()
