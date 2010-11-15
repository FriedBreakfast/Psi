#ifndef HPP_PSI_TVM_ASSEMBLER
#define HPP_PSI_TVM_ASSEMBLER

#include <map>

#include "Core.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      class AssemblerContext {
      public:
	AssemblerContext(Context *context);
	AssemblerContext(const AssemblerContext *parent);

	Context& context() const;
	const TermPtr<>& get(const std::string& name) const;
	void put(const std::string& name, const TermPtr<>& value);

      private:
	Context *m_context;
	const AssemblerContext *m_parent;
	std::map<std::string, TermPtr<> > terms;
      };

      TermPtr<> build_expression(AssemblerContext& context, const Parser::Expression& expression);
      TermPtr<FunctionTypeTerm> build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type);
      TermPtr<FunctionTerm> build_function(AssemblerContext& context, const Parser::Function& function);
      TermPtr<> build_call_expression(AssemblerContext& context, const Parser::Expression& expression);

      struct FunctionalTermAssembler {
	const char *name;
	TermPtr<FunctionalTerm> (*callback) (const char *name, AssemblerContext& context, const Parser::CallExpression& expression);
      };

      struct InstructionTermAssembler {
	const char *name;
	TermPtr<InstructionTerm> (*callback) (const char *name, BlockTerm& block, AssemblerContext& context, const Parser::CallExpression& expression);
      };

      extern FunctionalTermAssembler functional_ops[];
      extern InstructionTermAssembler instruction_ops[];
    }

    std::map<std::string, TermPtr<GlobalTerm> > build(Context& context, const boost::intrusive::list<Parser::NamedGlobalElement>& globals);
    std::map<std::string, TermPtr<GlobalTerm> > parse_and_build(Context& context, const char *begin, const char *end);
    std::map<std::string, TermPtr<GlobalTerm> > parse_and_build(Context& context, const char *begin);
  }
}

#endif
