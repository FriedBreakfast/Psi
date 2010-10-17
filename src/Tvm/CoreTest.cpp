#include <boost/test/unit_test.hpp>

#include "Core.hpp"
#include "Type.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_AUTO_TEST_SUITE(CoreTest)

    BOOST_AUTO_TEST_CASE(ConstructTest) {
      Context con;
    }

    BOOST_AUTO_TEST_CASE(IntTypeJIT) {
      Context con;

      TermPtr<> i16_t = con.get_functional_v(IntegerType(true, 16));
      TermPtr<GlobalTerm> gv16 = con.new_global_variable_set(i16_t, true);
      const MetatypeValue *ptr16 = static_cast<const MetatypeValue*>(con.term_jit(gv16));

      BOOST_CHECK_EQUAL(ptr16->size, sizeof(std::tr1::int16_t));
      BOOST_CHECK_EQUAL(ptr16->align, boost::alignment_of<std::tr1::int16_t>::value);

      TermPtr<> i64_t = con.get_functional_v(IntegerType(true, 64));
      TermPtr<GlobalTerm> gv64 = con.new_global_variable_set(i64_t, true);
      const MetatypeValue *ptr64 = static_cast<const MetatypeValue*>(con.term_jit(gv64));
     
      BOOST_CHECK_EQUAL(ptr64->size, sizeof(std::tr1::int64_t));
      BOOST_CHECK_EQUAL(ptr64->align, boost::alignment_of<std::tr1::int64_t>::value);
    }

    BOOST_AUTO_TEST_CASE(IntValueJIT) {
      Context con;

      std::tr1::int32_t c = 4328950;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));
      TermPtr<GlobalTerm> gv = con.new_global_variable_set(value, true);
      const std::tr1::int32_t *p = static_cast<const std::tr1::int32_t*>(con.term_jit(gv));

      BOOST_CHECK_EQUAL(*p, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
