#include "ExpressionCompiler.hpp"

namespace Psi {
  namespace Compiler {
    LookupResult<MemberType::EvaluateCallback> MemberType::member_lookup(const std::string&) {
      return no_match;
    }

    LookupResult<MemberType::EvaluateCallback> MemberType::evaluate(const std::vector<Parser::Expression>&) {
      return no_match;
    }

    InstructionList MemberType::specialize(const Context&,
                                           const std::vector<Type>&,
                                           const std::shared_ptr<Value>&) {
      throw std::logic_error("not implemented");
    }

    namespace {
      const Parser::Expression& validate_call_arguments(const std::vector<Parser::Expression>& arguments) {
        if (arguments.size() != 1)
          throw std::logic_error("Function call expects a single argument block");

        const Parser::Expression& first = arguments.front();

        if (first.which() != Parser::ExpressionType::token)
          throw std::logic_error("Argument to function call should be a token");

        if (first.token().token_type() != Parser::TokenType::bracket)
          throw std::logic_error("Argument to function call should be (...)");

        return first;
      }

      class FunctionMember : public MemberType {
      public:
        virtual LookupResult<EvaluateCallback> evaluate(const std::vector<Parser::Expression>& arguments) {
          return [] (const Value& value, const EvaluateContext& context, const SourceLocation& source) -> Value {
          };
        }
      };
    }
  }
}
