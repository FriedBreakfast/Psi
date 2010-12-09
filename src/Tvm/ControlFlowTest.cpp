#include "Test.hpp"

#include "Core.hpp"
#include "Function.hpp"
#include "Functional.hpp"
#include "Number.hpp"
#include "ControlFlow.hpp"
#include "Primitive.hpp"
#include "JitTypes.hpp"

#include <boost/format.hpp>

namespace Psi {
  namespace Tvm {
    BOOST_FIXTURE_TEST_SUITE(ControlFlowTest, Test::ContextFixture)

    BOOST_AUTO_TEST_CASE(ReturnIntConst) {
      const Jit::Int32 c = 614659930;

      IntegerType i32(true, 32);
      Term* i32_t = context.get_functional_v(i32);
      Term* value = context.get_functional_v(ConstantInteger(i32, c));

      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i32_t);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), value);

      typedef void (*callback_type) (void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      Jit::Int32 result;
      callback(&result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ReturnIntParameter) {
      const Jit::Int32 c = 143096367;

      Term* i32_t = context.get_functional_v(IntegerType(true, 32));
      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i32_t, i32_t);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), func->parameter(0));

      typedef void (*callback_type) (void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      Jit::Int32 result;
      callback(&result, &c);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ReturnDependent) {
      // a decent data size is required - previously a test of less
      // than 16 bytes worked previously for no known reason even
      // though the code generation wasn't working properly.
      const char data[] = "f4oh3g10845XweNNyu19hgb19";
      const Jit::Metatype data_meta = {sizeof(data), 1};
      BOOST_TEST_MESSAGE(boost::format("Fake type size: %d") % data_meta.size);

      FunctionTypeParameterTerm* param1 = context.new_function_type_parameter(context.get_metatype());
      FunctionTypeParameterTerm* param2 = context.new_function_type_parameter(param1);
      FunctionTypeTerm* func_type = context.get_function_type_v(param1, param1, param2);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);
      entry->new_instruction_v(Return(), func->parameter(1));

      typedef void (*callback_type) (void*,const void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      char result_data[sizeof(data)];
      // Set to an incorrect value
      std::fill_n(result_data, sizeof(result_data), 'x');
      callback(result_data, &data_meta, data);
      BOOST_CHECK_EQUAL_COLLECTIONS(result_data, result_data+sizeof(result_data),
				    data, data+sizeof(data));
    }

    BOOST_AUTO_TEST_CASE(UnconditionalBranchTest) {
      const Jit::Int32 c = 85278453;

      IntegerType i32(true, 32);
      Term* i32_t = context.get_functional_v(i32);
      Term* value = context.get_functional_v(ConstantInteger(i32, c));

      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i32_t);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);
      
      BlockTerm* branch_target = func->new_block(entry);
      entry->new_instruction_v(UnconditionalBranch(), branch_target);
      branch_target->new_instruction_v(Return(), value);

      typedef void (*callback_type) (void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      Jit::Int32 result;
      callback(&result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(ConditionalBranchTest) {
      const Jit::Int8 c1 = 31;
      const Jit::Int8 c2 = -47;

      IntegerType i8(true, 8);
      Term* i8_t = context.get_functional_v(i8);
      Term* bool_t = context.get_functional_v(BooleanType());

      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i8_t, bool_t);
      FunctionTerm* func = context.new_function(func_type, "f");
      BlockTerm* entry = func->new_block();
      func->set_entry(entry);

      BlockTerm* block1 = func->new_block(entry);
      BlockTerm* block2 = func->new_block(entry);

      entry->new_instruction_v(ConditionalBranch(), func->parameter(0), block1, block2);
      block1->new_instruction_v(Return(), context.get_functional_v(ConstantInteger(i8, c1)));
      block2->new_instruction_v(Return(), context.get_functional_v(ConstantInteger(i8, c2)));

      typedef void (*callback_type) (void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(func));

      Jit::Int8 result;
      Jit::Int8 param = 1;
      callback(&result, &param);
      BOOST_CHECK_EQUAL(result, c1);

      param = 0;
      callback(&result, &param);
      BOOST_CHECK_EQUAL(result, c2);
    }

    BOOST_AUTO_TEST_CASE(RecursiveCall) {
      const Jit::Int32 c = 275894789;

      IntegerType i32(true, 32);
      Term* i32_t = context.get_functional_v(i32);
      Term* value = context.get_functional_v(ConstantInteger(i32, c));

      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i32_t);
      FunctionTerm* outer = context.new_function(func_type, "outer");
      FunctionTerm* inner = context.new_function(func_type, "inner");

      BlockTerm* outer_entry = outer->new_block();
      BlockTerm* inner_entry = inner->new_block();

      outer->set_entry(outer_entry);
      inner->set_entry(inner_entry);

      Term* call_value = outer_entry->new_instruction_v(FunctionCall(), inner);
      outer_entry->new_instruction_v(Return(), call_value);
      inner_entry->new_instruction_v(Return(), value);

      typedef void (*callback_type) (void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(outer));

      Jit::Int32 result;
      callback(&result);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_CASE(RecursiveCallParameter) {
      const Jit::Int32 c = 758723;

      IntegerType i32(true, 32);
      Term* i32_t = context.get_functional_v(i32);

      FunctionTypeTerm* func_type = context.get_function_type_fixed_v(i32_t, i32_t);
      FunctionTerm* outer = context.new_function(func_type, "outer");
      FunctionTerm* inner = context.new_function(func_type, "inner");

      BlockTerm* outer_entry = outer->new_block();
      BlockTerm* inner_entry = inner->new_block();

      outer->set_entry(outer_entry);
      inner->set_entry(inner_entry);

      Term* call_value = outer_entry->new_instruction_v(FunctionCall(), inner, outer->parameter(0));
      outer_entry->new_instruction_v(Return(), call_value);
      inner_entry->new_instruction_v(Return(), inner->parameter(0));

      typedef void (*callback_type) (void*,const void*);
      callback_type callback = reinterpret_cast<callback_type>(context.term_jit(outer));

      Jit::Int32 result;
      callback(&result, &c);
      BOOST_CHECK_EQUAL(result, c);
    }

    BOOST_AUTO_TEST_SUITE_END()
  }
}
