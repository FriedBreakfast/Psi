#include "Test.hpp"

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Number.hpp"
#include "Primitive.hpp"
#include "Derived.hpp"
#include "JitTypes.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(CoreTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(EmptyStructTest) {
      Term* empty_struct_type = context.get_struct_type(ArrayPtr<Term*const>(NULL,0));
      GlobalTerm *empty_struct_meta = context.new_global_variable_set(empty_struct_type, true, "v1");
      const Jit::Metatype *esm = static_cast<const Jit::Metatype*>(context.term_jit(empty_struct_meta));

      BOOST_CHECK_EQUAL(esm->size, 0);
      BOOST_CHECK_EQUAL(esm->align, 1);
    }

    BOOST_AUTO_TEST_CASE(IntType) {
      Term* i16_t = context.get_functional_v(IntegerType(true, 16));
      GlobalTerm* gv16 = context.new_global_variable_set(i16_t, true, "v1");
      const Jit::Metatype *ptr16 = static_cast<const Jit::Metatype*>(context.term_jit(gv16));

      BOOST_CHECK_EQUAL(ptr16->size, sizeof(Jit::Int16));
      BOOST_CHECK_EQUAL(ptr16->align, boost::alignment_of<Jit::Int16>::value);

      Term* i64_t = context.get_functional_v(IntegerType(true, 64));
      GlobalTerm* gv64 = context.new_global_variable_set(i64_t, true, "v2");
      const Jit::Metatype *ptr64 = static_cast<const Jit::Metatype*>(context.term_jit(gv64));
     
      BOOST_CHECK_EQUAL(ptr64->size, sizeof(Jit::Int64));
      BOOST_CHECK_EQUAL(ptr64->align, boost::alignment_of<Jit::Int64>::value);
    }

    BOOST_AUTO_TEST_CASE(IntValue) {
      Jit::Int32 c = 4328950;

      IntegerType i32(true, 32);
      Term* value = context.get_functional_v(ConstantInteger(i32, c));
      GlobalTerm* gv = context.new_global_variable_set(value, true, "v1");
      const Jit::Int32 *p = static_cast<const Jit::Int32*>(context.term_jit(gv));

      BOOST_CHECK_EQUAL(*p, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
