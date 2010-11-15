#include <boost/test/unit_test.hpp>
#include <boost/checked_delete.hpp>

#include "Parser.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    namespace Parser {
      BOOST_AUTO_TEST_SUITE(TvmParserTest)

      BOOST_AUTO_TEST_CASE(GlobalConst) {
	const char *src = "%x = const (const_int #32 #0);";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_define);
      }

      BOOST_AUTO_TEST_CASE(GlobalVariableExtern) {
	const char *src = "%x = global (int #32);";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_variable);
      }

      BOOST_AUTO_TEST_CASE(GlobalVariable) {
	const char *src = "%x = global (int #32) (const_int #32 #0);";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_variable);
      }

      BOOST_AUTO_TEST_CASE(FunctionExtern) {
	const char *src = "%x = function ((int #32), (int #64)) > (int #16);";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_function);
      }

      BOOST_AUTO_TEST_CASE(Function) {
	const char *src =
	  "%x = function ((int #32), (int #64)) > (int #16) {\n"
	  "   return (int #16 #0);"
	  "};\n";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_function);
      }

      BOOST_AUTO_TEST_SUITE_END()
    }
  }
}
