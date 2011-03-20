#ifndef HPP_PSI_TVM_ASSEMBLER
#define HPP_PSI_TVM_ASSEMBLER

#include <exception>
#include <boost/function.hpp>
#include <tr1/unordered_map>

#include "Core.hpp"
#include "Parser.hpp"
#include "InstructionBuilder.hpp"

namespace Psi {
  namespace Tvm {
    class InstructionTerm;
    class BlockTerm;

    namespace Assembler {
      /**
       * Thrown when a syntactic error is detected in the
       * assembler. Semantic errors will be raised using TvmUserError.
       */
      class AssemblerError : public std::exception {
      public:
	explicit AssemblerError(const std::string& msg);
	virtual ~AssemblerError() throw ();
	virtual const char* what() const throw();

      private:
	const char *m_str;
	std::string m_message;
      };

      class AssemblerContext {
      public:
	AssemblerContext(Module *module);
	AssemblerContext(const AssemblerContext *parent);

        Module& module() const;
	Context& context() const;
	Term* get(const std::string& name) const;
	void put(const std::string& name, Term* value);

      private:
        Module *m_module;
	const AssemblerContext *m_parent;
        typedef std::tr1::unordered_map<std::string, Term*> TermMap;
	TermMap m_terms;
      };

      Term* build_expression(AssemblerContext& context, const Parser::Expression& expression);
      FunctionTypeTerm* build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type);
      FunctionTerm* build_function(AssemblerContext& context, const Parser::Function& function);
      Term* build_call_expression(AssemblerContext& context, const Parser::Expression& expression);

      typedef boost::function<Term*(const std::string&,AssemblerContext&,const Parser::CallExpression&)> FunctionalTermCallback;
      typedef boost::function<InstructionTerm*(const std::string&,InstructionBuilder&,AssemblerContext&,const Parser::CallExpression&)> InstructionTermCallback;

      extern const std::tr1::unordered_map<std::string, FunctionalTermCallback> functional_ops;
      extern const std::tr1::unordered_map<std::string, InstructionTermCallback> instruction_ops;
    }

    typedef std::tr1::unordered_map<std::string, GlobalTerm*> AssemblerResult;

    AssemblerResult build(Module&, const boost::intrusive::list<Parser::NamedGlobalElement>&);
    AssemblerResult parse_and_build(Module&, const char*, const char*);
    AssemblerResult parse_and_build(Module&, const char*);
  }
}

#endif
