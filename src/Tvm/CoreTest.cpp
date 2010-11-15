#include <boost/test/unit_test.hpp>

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Type.hpp"
#include "Number.hpp"
#include "Primitive.hpp"
#include "JitTypes.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_AUTO_TEST_SUITE(CoreTest)

    BOOST_AUTO_TEST_CASE(ConstructTest) {
      Context con;
      BOOST_TEST_MESSAGE("Constructed context successfully");
    }

    BOOST_AUTO_TEST_CASE(IntType) {
      Context con;

      TermPtr<> i16_t = con.get_functional_v(IntegerType(true, 16));
      TermPtr<GlobalTerm> gv16 = con.new_global_variable_set(i16_t, true);
      const Jit::Metatype *ptr16 = static_cast<const Jit::Metatype*>(con.term_jit(gv16));

      BOOST_CHECK_EQUAL(ptr16->size, sizeof(Jit::Int16));
      BOOST_CHECK_EQUAL(ptr16->align, boost::alignment_of<Jit::Int16>::value);

      TermPtr<> i64_t = con.get_functional_v(IntegerType(true, 64));
      TermPtr<GlobalTerm> gv64 = con.new_global_variable_set(i64_t, true);
      const Jit::Metatype *ptr64 = static_cast<const Jit::Metatype*>(con.term_jit(gv64));
     
      BOOST_CHECK_EQUAL(ptr64->size, sizeof(Jit::Int64));
      BOOST_CHECK_EQUAL(ptr64->align, boost::alignment_of<Jit::Int64>::value);
    }

    BOOST_AUTO_TEST_CASE(IntValue) {
      Context con;

      Jit::Int32 c = 4328950;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));
      TermPtr<GlobalTerm> gv = con.new_global_variable_set(value, true);
      const Jit::Int32 *p = static_cast<const Jit::Int32*>(con.term_jit(gv));

      BOOST_CHECK_EQUAL(*p, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
