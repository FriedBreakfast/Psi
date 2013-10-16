#include <boost/checked_delete.hpp>

#include "Test.hpp"
#include "Parser.hpp"
#include "../Utility.hpp"

namespace Psi {
  namespace Tvm {
    namespace Parser {
      PSI_TEST_SUITE_FIXTURE(TvmParserTest, Test::ContextFixture)

      PSI_TEST_CASE(GlobalConst) {
        const char *src = "%x = define #i0;";

        PSI_STD::vector<NamedGlobalElement> result = parse(error_context, location, src);

        PSI_TEST_CHECK_EQUAL(result.size(), 1u);
        PSI_TEST_CHECK_EQUAL(result.front().value->global_type, global_define);
      }

      PSI_TEST_CASE(GlobalVariableExtern) {
        const char *src = "%x = global const i32;";

        PSI_STD::vector<NamedGlobalElement> result =  parse(error_context, location, src);

        PSI_TEST_CHECK_EQUAL(result.size(), 1u);
        PSI_TEST_CHECK_EQUAL(result.front().value->global_type, global_variable);
      }

      PSI_TEST_CASE(GlobalVariable) {
        const char *src = "%x = global i32 #i0;";

        PSI_STD::vector<NamedGlobalElement> result = parse(error_context, location, src);

        PSI_TEST_CHECK_EQUAL(result.size(), 1u);
        PSI_TEST_CHECK_EQUAL(result.front().value->global_type, global_variable);
      }

      PSI_TEST_CASE(FunctionExtern) {
        const char *src = "%x = function (i32, i64) > i16;";

        PSI_STD::vector<NamedGlobalElement> result = parse(error_context, location, src);

        PSI_TEST_CHECK_EQUAL(result.size(), 1u);
        PSI_TEST_CHECK_EQUAL(result.front().value->global_type, global_function);
      }

      PSI_TEST_CASE(Function) {
        const char *src =
          "%x = function (i32, i64) > i16 {\n"
          "   return #i0;"
          "};\n";

        PSI_STD::vector<NamedGlobalElement> result = parse(error_context, location, src);

        PSI_TEST_CHECK_EQUAL(result.size(), 1u);
        PSI_TEST_CHECK_EQUAL(result.front().value->global_type, global_function);
      }

      PSI_TEST_SUITE_END()
    }
  }
}
