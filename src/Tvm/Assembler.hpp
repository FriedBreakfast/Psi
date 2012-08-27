#ifndef HPP_PSI_TVM_ASSEMBLER
#define HPP_PSI_TVM_ASSEMBLER

#include <exception>
#include <boost/function.hpp>

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
        ValuePtr<> get(const std::string& name) const;
        void put(const std::string& name, const ValuePtr<>& value);

      private:
        Module *m_module;
        const AssemblerContext *m_parent;
        typedef boost::unordered_map<std::string, ValuePtr<> > TermMap;
        TermMap m_terms;
      };

      ValuePtr<> build_expression(AssemblerContext& context, const Parser::Expression& expression, const LogicalSourceLocationPtr& location);
      ValuePtr<FunctionType> build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type, const LogicalSourceLocationPtr& location);
      ValuePtr<RecursiveType> build_recursive_type(AssemblerContext& context,  const Parser::RecursiveType& recursive_type, const LogicalSourceLocationPtr& location);
      void build_function(AssemblerContext& context, const ValuePtr<Function>& function, const Parser::Function& function_def);
      ValuePtr<> build_call_expression(AssemblerContext& context, const Parser::Expression& expression, const LogicalSourceLocationPtr& location);
      std::vector<ValuePtr<ParameterPlaceholder> > build_parameters(AssemblerContext& context,
                                                                    const UniqueList<Parser::NamedExpression>& parameters,
                                                                    const LogicalSourceLocationPtr& logical_location);

      typedef boost::function<ValuePtr<>(const std::string&,AssemblerContext&,const Parser::CallExpression&,const LogicalSourceLocationPtr&)> FunctionalTermCallback;
      typedef boost::function<ValuePtr<Instruction>(const std::string&,InstructionBuilder&,AssemblerContext&,const Parser::CallExpression&,const LogicalSourceLocationPtr&)> InstructionTermCallback;

      extern const boost::unordered_map<std::string, FunctionalTermCallback> functional_ops;
      extern const boost::unordered_map<std::string, InstructionTermCallback> instruction_ops;
    }

    typedef boost::unordered_map<std::string, ValuePtr<> > AssemblerResult;

    AssemblerResult build(Module&, const boost::intrusive::list<Parser::NamedGlobalElement>&);
    AssemblerResult parse_and_build(Module&, const char*, const char*);
    AssemblerResult parse_and_build(Module&, const char*);
  }
}

#endif
