#include <boost/test/unit_test.hpp>
#include <boost/format.hpp>

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
    BOOST_AUTO_TEST_SUITE(ControlFlowTest)

    BOOST_AUTO_TEST_CASE(ReturnIntConst) {
      Context con;

      const Jit::Int32 c = 614659930;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));

      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i32_t);
      TermPtr<FunctionTerm> func = con.new_function(func_type);
      func->entry()->new_instruction_v(Return(), value);

      typedef void* (*callback_type) (void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      Jit::Int32 result;
      void *result_ptr = callback(&result);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ReturnIntParameter) {
      Context con;

      const Jit::Int32 c = 143096367;

      TermPtr<> i32_t = con.get_functional_v(IntegerType(true, 32));
      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i32_t, i32_t);
      TermPtr<FunctionTerm> func = con.new_function(func_type);
      func->entry()->new_instruction_v(Return(), func->parameter(0));

      typedef void* (*callback_type) (void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      Jit::Int32 result;
      void *result_ptr = callback(&result, &c);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ReturnDependent) {
      Context con;

      // a decent data size is required - previously a test of less
      // than 16 bytes worked previously for no known reason even
      // though the code generation wasn't working properly.
      const char data[] = "f4oh3g10845XweNNyu19hgb19";
      const Jit::Metatype data_meta = {sizeof(data), 1};
      BOOST_TEST_MESSAGE(boost::format("Fake type size: %d") % data_meta.size);

      TermPtr<> metatype = con.get_metatype();
      TermPtr<FunctionTypeParameterTerm> param1 = con.new_function_type_parameter(con.get_metatype());
      TermPtr<FunctionTypeParameterTerm> param2 = con.new_function_type_parameter(param1);
      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_v(param1, param1, param2);
      TermPtr<FunctionTerm> func = con.new_function(func_type);
      func->entry()->new_instruction_v(Return(), func->parameter(1));

      typedef void* (*callback_type) (void*,const void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      char result_data[sizeof(data)];
      // Set to an incorrect value
      std::fill_n(result_data, sizeof(result_data), 'x');
      void *result_ptr = callback(result_data, &data_meta, data);
      BOOST_CHECK_EQUAL(result_ptr, result_data);
      BOOST_CHECK_EQUAL_COLLECTIONS(result_data, result_data+sizeof(result_data),
				    data, data+sizeof(data));
    }

    BOOST_AUTO_TEST_CASE(UnconditionalBranchTest) {
      Context con;

      const Jit::Int32 c = 85278453;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));

      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i32_t);
      TermPtr<FunctionTerm> func = con.new_function(func_type);
      
      TermPtr<BlockTerm> branch_target = func->new_block();
      func->entry()->new_instruction_v(UnconditionalBranch(), branch_target);
      branch_target->new_instruction_v(Return(), value);

      typedef void* (*callback_type) (void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      Jit::Int32 result;
      void *result_ptr = callback(&result);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ConditionalBranchTest) {
      Context con;

      const Jit::Int8 c1 = 31;
      const Jit::Int8 c2 = -47;

      IntegerType i8(true, 8);
      TermPtr<> i8_t = con.get_functional_v(i8);
      TermPtr<> bool_t = con.get_functional_v(BooleanType());

      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i8_t, bool_t);
      TermPtr<FunctionTerm> func = con.new_function(func_type);

      TermPtr<BlockTerm> block1 = func->new_block();
      TermPtr<BlockTerm> block2 = func->new_block();

      func->entry()->new_instruction_v(ConditionalBranch(), func->parameter(0), block1, block2);
      block1->new_instruction_v(Return(), con.get_functional_v(ConstantInteger(i8, c1)));
      block2->new_instruction_v(Return(), con.get_functional_v(ConstantInteger(i8, c2)));

      typedef void* (*callback_type) (void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(func));

      Jit::Int8 result;
      void *result_ptr;
      Jit::Int8 param = 1;
      result_ptr = callback(&result, &param);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c1);

      param = 0;
      callback(&result, &param);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c2);
    }

    BOOST_AUTO_TEST_CASE(RecursiveCall) {
      Context con;

      const Jit::Int32 c = 275894789;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));

      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i32_t);
      TermPtr<FunctionTerm> outer = con.new_function(func_type);
      TermPtr<FunctionTerm> inner = con.new_function(func_type);

      TermPtr<> call_value = outer->entry()->new_instruction_v(FunctionCall(), inner);
      outer->entry()->new_instruction_v(Return(), call_value);
      inner->entry()->new_instruction_v(Return(), value);

      typedef void* (*callback_type) (void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(outer));

      Jit::Int32 result;
      void *result_ptr = callback(&result);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(RecursiveCallParameter) {
      Context con;

      const Jit::Int32 c = 758723;

      IntegerType i32(true, 32);
      TermPtr<> i32_t = con.get_functional_v(i32);
      TermPtr<> value = con.get_functional_v(ConstantInteger(i32, c));

      TermPtr<FunctionTypeTerm> func_type = con.get_function_type_fixed_v(i32_t, i32_t);
      TermPtr<FunctionTerm> outer = con.new_function(func_type);
      TermPtr<FunctionTerm> inner = con.new_function(func_type);

      TermPtr<> call_value = outer->entry()->new_instruction_v(FunctionCall(), inner, outer->parameter(0));
      outer->entry()->new_instruction_v(Return(), call_value);
      inner->entry()->new_instruction_v(Return(), inner->parameter(0));

      typedef void* (*callback_type) (void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(con.term_jit(outer));

      Jit::Int32 result;
      void *result_ptr = callback(&result, &c);
      BOOST_CHECK_EQUAL(result_ptr, &result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
