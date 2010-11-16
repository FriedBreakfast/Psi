#ifndef HPP_PSI_TVM_ASSEMBLER
#define HPP_PSI_TVM_ASSEMBLER

#include <boost/function.hpp>
#include <tr1/unordered_map>

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
        typedef std::tr1::unordered_map<std::string, TermPtr<> > TermMap;
	TermMap m_terms;
      };

      TermPtr<> build_expression(AssemblerContext& context, const Parser::Expression& expression);
      TermPtr<FunctionTypeTerm> build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type);
      TermPtr<FunctionTerm> build_function(AssemblerContext& context, const Parser::Function& function);
      TermPtr<> build_call_expression(AssemblerContext& context, const Parser::Expression& expression);

      typedef boost::function<TermPtr<FunctionalTerm>(const std::string&,AssemblerContext&,const Parser::CallExpression&)> FunctionalTermCallback;
      typedef boost::function<TermPtr<InstructionTerm>(const std::string&,BlockTerm&,AssemblerContext&,const Parser::CallExpression&)> InstructionTermCallback;

      extern const std::tr1::unordered_map<std::string, FunctionalTermCallback> functional_ops;
      extern const std::tr1::unordered_map<std::string, InstructionTermCallback> instruction_ops;
    }

    typedef std::tr1::unordered_map<std::string, TermPtr<GlobalTerm> > AssemblerResult;

    AssemblerResult build(Context& context, const boost::intrusive::list<Parser::NamedGlobalElement>& globals);
    AssemblerResult parse_and_build(Context& context, const char *begin, const char *end);
    AssemblerResult parse_and_build(Context& context, const char *begin);
  }
}

#endif
