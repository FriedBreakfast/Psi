#include <boost/test/unit_test.hpp>

#include "TypeSystem.hpp"

#include <sstream>

#define FOR_ALL "\u2200"
#define EXISTS "\u2203"
#define ARROW "\u2192"

using namespace Psi::TypeSystem;

namespace {
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

    void check_apply(const Type& expected, const Type& function, const std::vector<Type>& parameters) {
      auto result = do_apply(function, parameters);

      BOOST_CHECK(result);
      if (result) {
        BOOST_CHECK_MESSAGE(*result == expected,
                            "\"" << print(*result, m_namer.namer) << "\" != \"" << print(expected, m_namer.namer) << "\"");
      }                
    }

    void check_apply_fail(const Type& function, const std::vector<Type>& parameters) {
      auto result = do_apply(function, parameters);
      BOOST_CHECK(!result);
    }

  private:
    MapNamer m_namer;

    struct ParameterPrinter {
      friend std::ostream& operator << (std::ostream& os, const ParameterPrinter& printer) {
        for (auto it = printer.parameters->begin(); it != printer.parameters->end(); ++it)
          os << " // " << print(*it, *printer.namer);
        return os;
      }

      const std::vector<Type> *parameters;
      TermNamer *namer;
    };

    Psi::Maybe<Type> do_apply(const Type& function, const std::vector<Type>& parameters) {
      std::unordered_map<unsigned, Type> parameters_map;
      unsigned pos = 0;
      for (auto it = parameters.begin(); it != parameters.end(); ++it, ++pos)
        parameters_map.insert({pos, *it});

      auto result = function_apply(function, parameters_map);

      ParameterPrinter parameter_printer = {&parameters, &m_namer.namer};
      if (result) {
        BOOST_TEST_MESSAGE(print(function, m_namer.namer) << parameter_printer << " ==> " << print(*result, m_namer.namer));
      } else {
        BOOST_TEST_MESSAGE(print(function, m_namer.namer) << parameter_printer << " failed");
      }

      return result;
    }
  };
}

BOOST_FIXTURE_TEST_SUITE(TypeSystemTest, Fixture)

BOOST_AUTO_TEST_CASE(TestPrint) {
  auto q1 = Variable::new_();

  auto x = Variable::new_();
  std::stringstream ss;

  MapNamer namer;
  namer.variables.insert({x, "x"});

  ss << print(x, namer.namer);
  BOOST_CHECK_EQUAL(ss.str(), "x");
  ss.str("");

  ss << print(for_all({q1}, q1), namer.namer);
  BOOST_CHECK_EQUAL(ss.str(), FOR_ALL " a.a");
  ss.str("");

  ss << print(for_all({q1}, implies({Type(q1)}, q1)), namer.namer);
  BOOST_CHECK_EQUAL(ss.str(), FOR_ALL " a.a " ARROW " a");
  ss.str("");
}

BOOST_AUTO_TEST_CASE(TestApplyId) {
  check_apply(P,
              for_all({x}, implies({Type(x)}, x)),
              {Type(P)});
}

BOOST_AUTO_TEST_CASE(TestApplyId2) {
  check_apply(implies({Type(P)}, Q),
              for_all({x, y}, implies({Type(x), Type(y), Type(x)}, y)),
              {Type(P), Type(Q)});
}

BOOST_AUTO_TEST_CASE(TestSpecializeTerm) {
  check_apply(for_all({x},x),
              for_all({x},implies({Type(P)},x)),
              {for_all({y},y)});
}

BOOST_AUTO_TEST_CASE(TestSpecializeFunction) {
  check_apply(Q,
              implies({for_all({x}, implies({Type(x), Type(P)}, P))}, Q),
              {for_all({x, y}, implies({Type(x), Type(y)}, y))});
}

BOOST_AUTO_TEST_CASE(TestSpecializeFunction2) {
  check_apply(P,
              for_all({y}, implies({for_all({x}, implies({Type(x), Type(y)}, y))}, y)),
              {for_all({x}, implies({Type(x), Type(P)}, P))});
}

BOOST_AUTO_TEST_CASE(TestSpecializeFunction3) {
  check_apply(for_all({x}, x),
              for_all({y}, implies({for_all({x}, implies({Type(x), Type(y)}, y))}, y)),
              {for_all({x, y}, implies({Type(x), Type(y)}, y))});
}

BOOST_AUTO_TEST_CASE(TestSpecializeFunctionFail) {
  check_apply_fail(implies({for_all({x, y}, implies({Type(x), Type(y)}, y))}, P),
                   {for_all({x}, implies({Type(x), Type(P)}, P))});
}

BOOST_AUTO_TEST_CASE(TestMoveForAll) {
  check_apply(for_all({x}, x),
              for_all({x}, implies({Type(P)}, x)),
              {Type(P)});
}

BOOST_AUTO_TEST_CASE(TestMoveForAll2) {
  check_apply(for_all({x}, x),
              for_all({x}, implies({Type(x)}, x)),
              {for_all({y}, y)});
}

BOOST_AUTO_TEST_SUITE_END()
