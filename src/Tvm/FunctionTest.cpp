#include <boost/test/unit_test.hpp>

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Type.hpp"
#include "Number.hpp"
#include "ControlFlow.hpp"
#include "Primitive.hpp"
#include "JitTypes.hpp"

namespace Psi {
  namespace Tvm {
    BOOST_AUTO_TEST_SUITE(FunctionTest)

    BOOST_AUTO_TEST_CASE(FunctionTypeTest) {
      Context con;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i32_t);

      BOOST_CHECK_EQUAL(func_type->calling_convention(), cconv_tvm);
      BOOST_CHECK_EQUAL(func_type->result_type(), i32_t); 
    }

    BOOST_AUTO_TEST_CASE(CCall_ReturnInt) {
      Context con;

      const Jit::Int32 c = 45878594;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));

      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(cconv_c, i32_t);
      TermPtr<FunctionTerm> func = con.new_function(func_type);
      TermPtr<BlockTerm> entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), value);

      typedef Jit::Int32 (*callback_type) ();
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      Jit::Int32 result = callback();
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(CCall_ReturnIntParameter) {
      Context con;

      const Jit::Int32 c = 258900654;

      TermPtr<> i32_t = con.get_functional_v(IntegerType(true, 32));
      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(cconv_c, i32_t, i32_t);
      TermPtr<FunctionTerm> func = con.new_function(func_type);
      TermPtr<BlockTerm> entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), func->parameter(0));

      typedef Jit::Int32 (*callback_type) (Jit::Int32);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      Jit::Int32 result = callback(c);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
 }
}
