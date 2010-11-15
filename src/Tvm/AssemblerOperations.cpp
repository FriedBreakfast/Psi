#include "Assembler.hpp"

#include "ControlFlow.hpp"
#include "Number.hpp"

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      void default_parameter_setup(TermPtrArray<>& parameters, const char *name, AssemblerContext& context, const Parser::CallExpression& expression) {
	if (!expression.values.empty())
	  throw std::logic_error(std::string(name) + " operation takes no parameters");

	PSI_ASSERT(parameters.size() == expression.terms.size());
	std::size_t n = 0;
	for (UniqueList<Parser::Expression>::const_iterator it = expression.terms.begin(); it != expression.terms.end(); ++n, ++it) {
	  PSI_ASSERT(n < parameters.size());
	  parameters.set(n, build_expression(context, *it));
	}
      }

      template<typename T>
      TermPtr<FunctionalTerm> default_functional_callback(const char *name, AssemblerContext& context, const Parser::CallExpression& expression) {
	TermPtrArray<> parameters(expression.terms.size());
	default_parameter_setup(parameters, name, context, expression);
	return context.context().get_functional(T(), parameters);
      }

      FunctionalTermAssembler functional_ops[] = {
	{"bool", default_functional_callback<BooleanType>}
      };

      template<typename T>
      TermPtr<InstructionTerm> default_instruction_callback(const char *name, BlockTerm& block, AssemblerContext& context, const Parser::CallExpression& expression) {
	TermPtrArray<> parameters(expression.terms.size());
	default_parameter_setup(parameters, name, context, expression);
	return block.new_instruction(T(), parameters);
      }

      InstructionTermAssembler instruction_ops[] = {
	{"br", default_instruction_callback<UnconditionalBranch>},
	{"call", default_instruction_callback<FunctionCall>},
	{"cond_br", default_instruction_callback<ConditionalBranch>},
	{"return", default_instruction_callback<Return>}
      };
    }
  }
}
