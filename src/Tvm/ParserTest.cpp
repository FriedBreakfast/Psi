#include <boost/test/unit_test.hpp>
#include <boost/checked_delete.hpp>

#include "Parser.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    namespace Parser {
      BOOST_AUTO_TEST_SUITE(TvmParserTest)

      BOOST_AUTO_TEST_CASE(GlobalConst) {
	const char *src = "%x = define #i0;";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_define);
      }

      BOOST_AUTO_TEST_CASE(GlobalVariableExtern) {
	const char *src = "%x = global const i32;";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_variable);
      }

      BOOST_AUTO_TEST_CASE(GlobalVariable) {
	const char *src = "%x = global i32 #i0;";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_variable);
      }

      BOOST_AUTO_TEST_CASE(FunctionExtern) {
	const char *src = "%x = function (i32, i64) > i16;";

	UniqueList<NamedGlobalElement> result;
	parse(result, src);

	BOOST_CHECK_EQUAL(result.size(), 1);
	BOOST_CHECK_EQUAL(result.front().value->global_type, global_function);
      }

      BOOST_AUTO_TEST_CASE(Function) {
	const char *src =
	  "%x = function (i32, i64) > i16 {\n"
	  "   return #i0;"
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
