#include <boost/test/unit_test.hpp>

#include "Core.hpp"
#include "Type.hpp"
#include "Constant.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_AUTO_TEST_SUITE(CoreTest)

    BOOST_AUTO_TEST_CASE(ConstructTest) {
      Context con;
    }

    BOOST_AUTO_TEST_CASE(IntTypeJIT) {
      Context con;

      Term *i16 = con.type_int16();
      GlobalVariable *gv16 = GlobalVariable::create(con.metatype(), true, i16);
      const MetatypeValue *ptr16 = static_cast<const MetatypeValue*>(con.term_jit(gv16));
     
      BOOST_CHECK_EQUAL(ptr16->size, sizeof(std::tr1::int16_t));
      BOOST_CHECK_EQUAL(ptr16->align, align_of<std::tr1::int16_t>());

      Term *i64 = con.type_int64();
      GlobalVariable *gv64 = GlobalVariable::create(con.metatype(), true, i64);
      const MetatypeValue *ptr64 = static_cast<const MetatypeValue*>(con.term_jit(gv64));
     
      BOOST_CHECK_EQUAL(ptr64->size, sizeof(std::tr1::int64_t));
      BOOST_CHECK_EQUAL(ptr64->align, align_of<std::tr1::int64_t>());
    }

    BOOST_AUTO_TEST_CASE(IntValueJIT) {
      Context con;

      std::tr1::int32_t c = 4328950;

      IntegerType *i32 = con.type_int32();
      Term *value = ConstantInteger::create(i32, c);
      GlobalVariable *gv = GlobalVariable::create(i32, true, value);
      const std::tr1::int32_t *p = static_cast<const std::tr1::int32_t*>(con.term_jit(gv));

      BOOST_CHECK_EQUAL(*p, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
