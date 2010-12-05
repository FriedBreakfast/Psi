#ifndef HPP_PSI_TVM_ASSEMBLER
#define HPP_PSI_TVM_ASSEMBLER

#include <stdexcept>
#include <boost/function.hpp>
#include <tr1/unordered_map>

#include "Core.hpp"
#include "Parser.hpp"

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      /**
       * Thrown when a syntactic error is detected in the
       * assembler. Semantic errors will be raised using TvmUserError.
       */
      class AssemblerError : public std::runtime_error {
      public:
        AssemblerError(const std::string& msg);
      };

      class AssemblerContext {
      public:
	AssemblerContext(Context *context);
	AssemblerContext(const AssemblerContext *parent);

	Context& context() const;
	Term* get(const std::string& name) const;
	void put(const std::string& name, Term* value);

      private:
	Context *m_context;
	const AssemblerContext *m_parent;
        typedef std::tr1::unordered_map<std::string, Term*> TermMap;
	TermMap m_terms;
      };

      Term* build_expression(AssemblerContext& context, const Parser::Expression& expression);
      FunctionTypeTerm* build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type);
      FunctionTerm* build_function(AssemblerContext& context, const Parser::Function& function);
      Term* build_call_expression(AssemblerContext& context, const Parser::Expression& expression);

      typedef boost::function<FunctionalTerm*(const std::string&,AssemblerContext&,const Parser::CallExpression&)> FunctionalTermCallback;
      typedef boost::function<InstructionTerm*(const std::string&,BlockTerm&,AssemblerContext&,const Parser::CallExpression&)> InstructionTermCallback;

      extern const std::tr1::unordered_map<std::string, FunctionalTermCallback> functional_ops;
      extern const std::tr1::unordered_map<std::string, InstructionTermCallback> instruction_ops;
    }

    typedef std::tr1::unordered_map<std::string, GlobalTerm*> AssemblerResult;

    AssemblerResult build(Context& context, const boost::intrusive::list<Parser::NamedGlobalElement>& globals);
    AssemblerResult parse_and_build(Context& context, const char *begin, const char *end);
    AssemblerResult parse_and_build(Context& context, const char *begin);
  }
}

#endif
