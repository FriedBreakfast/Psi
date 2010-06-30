#include "ExpressionCompiler.hpp"
#include "Format.hpp"

#include <boost/lexical_cast.hpp>

namespace Psi {
  namespace Compiler {
    struct FunctionDescription {
      /// Name of the function
      std::string name;
      /// Number of arguments the function takes
      unsigned argument_count;
      /// Map from positional arguments given by the user to arguments to the underlying function
      std::vector<int> positional;
      /// Map from named arguments given by the user to arguments to the underlying function
      std::unordered_map<std::string, int> keywords;
    };

    LookupResult<MemberType::EvaluateCallback> MemberType::member_lookup(const std::string&) const {
      return no_match;
    }

    LookupResult<MemberType::EvaluateCallback> MemberType::evaluate(PointerList<const Parser::Expression>) const {
      return no_match;
    }

    namespace {
      std::string bracket_macro_name(Parser::TokenType tt) {
        switch (tt) {
        case Parser::TokenType::brace: return ":bracket";
        case Parser::TokenType::square_bracket: return ":squareBracket";
        case Parser::TokenType::bracket: return ":brace";
        default: throw std::logic_error("unknown bracket type");
        }
      }
    }

    /**
     * The same as #compile_expression, but includes an additional
     * parameter about whether to use exactly the given location or
     * an anonymous child location for the generated code.
     *
     * \param anonymize_location Whether to add an anonymous child
     * node to locations in the generated code.
     */
    CodeValue compile_expression(const Parser::Expression& expression, const EvaluateContext& context,
                                 const LogicalSourceLocation& source,
                                 bool anonymize_location=true) {
      SourceLocation location = {source, expression.source};

      LogicalSourceLocation first_source = anonymize_location ? LogicalSourceLocation::anonymous_child(source) : source;
        
      switch (expression.which()) {
      case Parser::ExpressionType::macro: {
        const auto& macro_expr = expression.macro();

        auto first = compile_expression(macro_expr.elements.at(0), context, first_source, false);
        auto first_member_ptr = first.value.type().user().cast<MemberType>();
        auto first_lookup = first_member_ptr->evaluate({macro_expr.elements, 1});

        if (first_lookup.conflict() || first_lookup.no_match())
          throw compile_error(format("Evaluation failed"));

        auto second = (*first_lookup)(first.value, context, location);

        return CodeValue{std::move(second.value), CodeBlock{std::move(first.code), std::move(second.code)}};
      }

      case Parser::ExpressionType::token: {
        const auto& token_expr = expression.token();

        switch (token_expr.token_type) {
        case Parser::TokenType::brace:
        case Parser::TokenType::square_bracket:
        case Parser::TokenType::bracket: {
          auto bracket_str = bracket_macro_name(token_expr.token_type);

          auto context_lookup = context.lookup(bracket_macro_name(token_expr.token_type));
          if (context_lookup.conflict() || context_lookup.no_match())
            throw compile_error(format("Context does not support evaluating %s brackets", bracket_macro_name(token_expr.token_type)));

          auto first = (*context_lookup)(location);
          auto first_member_ptr = first.value.type().user().cast<MemberType>();
          auto first_lookup = first_member_ptr->evaluate({expression});

          if (first_lookup.conflict() || first_lookup.no_match())
            throw compile_error(format("%s bracket evaluation failed", bracket_macro_name(token_expr.token_type)));

          auto second = (*first_lookup)(first.value, context, location);

          return {std::move(second.value), {std::move(first.code), std::move(second.code)}};
        }

        case Parser::TokenType::identifier: {
          auto name = token_expr.text.str();
          auto context_lookup = context.lookup(name);

          if (context_lookup.conflict() || context_lookup.no_match())
            throw compile_error(format("Name not found: %s", name));
            
          return (*context_lookup)(location);
        }

        default:
          throw std::logic_error("unknown token type");
        }
      }

      default:
        throw std::logic_error("unknown expression type");
      }
    }

    namespace {
      const Parser::Expression& validate_call_arguments(PointerList<const Parser::Expression> arguments) {
        if (arguments.size() != 1)
          throw std::logic_error("Function call expects a single argument block");

        const Parser::Expression& first = arguments.front();

        if (first.which() != Parser::ExpressionType::token)
          throw std::logic_error("Argument to function call should be a token");

        if (first.token().token_type != Parser::TokenType::bracket)
          throw std::logic_error("Argument to function call should be (...)");

        return first;
      }

      struct ParseCallArgumentsResult {
        std::vector<std::pair<int, Parser::Expression> > positional;
        std::vector<std::pair<std::string, Parser::Expression> > keywords;
      };

      ParseCallArgumentsResult parse_call_arguments(const Parser::Expression& parameters) {
        ParseCallArgumentsResult result;

        auto arguments = Parser::parse_argument_list(parameters.token().text);

        int positional_pos = 0;
        for (auto it = arguments.begin(); it != arguments.end(); ++it) {
          Maybe<int> index;
          if (it->name) {
            auto arg_name = it->name->str();
            try {
              index = boost::lexical_cast<int>(arg_name);
            } catch (boost::bad_lexical_cast&) {
            }

            if (!index)
              result.keywords.push_back({arg_name, std::move(it->value)});
          } else {
            index = positional_pos++;
          }

          if (index) {
            int index_unpacked = *index;
            result.positional.push_back({index_unpacked, std::move(it->value)});
            positional_pos = index_unpacked + 1;
          }
        }

        return result;
      }

      struct CompileCallArgumentsResult {
        CompileCallArgumentsResult() {}
        CompileCallArgumentsResult(const CompileCallArgumentsResult&) = delete;
        CompileCallArgumentsResult(CompileCallArgumentsResult&&) = default;

        CodeBlock code;
        std::unordered_map<unsigned, Value> positional;
        std::unordered_map<std::string, Value> keywords;
      };

      CompileCallArgumentsResult compile_call_arguments(const Parser::Expression& parameters, const EvaluateContext& context, const LogicalSourceLocation& location) {
        ParseCallArgumentsResult parse_result = parse_call_arguments(parameters);

        CompileCallArgumentsResult result;

        auto process_argument = [&] (const std::string& name, const Parser::Expression& argument) -> Value {
          auto code_value = compile_expression(argument, context, location);
          result.code.append(std::move(code_value.code));
          return code_value.value;
        };

        for (auto it = parse_result.positional.begin(); it != parse_result.positional.end(); ++it)
          result.positional.insert(std::make_pair(it->first, process_argument(boost::lexical_cast<std::string>(it->first), it->second)));

        for (auto it = parse_result.keywords.begin(); it != parse_result.keywords.end(); ++it)
          result.keywords.insert(std::make_pair(it->first, process_argument(it->first, it->second)));

        return result;
      }

      struct ApplyFunctionCommonResult {
        FunctionDescription description;
        std::unordered_map<unsigned, std::pair<Value, SourceLocation> > specified;
      };
        
      ApplyFunctionCommonResult apply_function_common(const FunctionDescription& function_description,
                                                      const std::unordered_map<unsigned, Value>& positional_arguments,
                                                      const std::unordered_map<std::string, Value>& keyword_arguments) {
        ApplyFunctionCommonResult result;

        for (auto it = positional_arguments.begin(); it != positional_arguments.end(); ++it) {
          if (it->first >= function_description.positional.size())
            throw std::runtime_error(format("No such positional argument: %d", it->first));

          auto position = function_description.positional.at(it->first);

          if (!result.specified.insert(std::make_pair(position, std::make_pair(it->second, SourceLocation()))).second)
            throw std::runtime_error(format("Multiple arguments passed to position %d", position));
        }

        for (auto it = keyword_arguments.begin(); it != keyword_arguments.end(); ++it) {
          auto position = function_description.keywords.find(it->first);

          if (position == function_description.keywords.end())
            throw std::runtime_error(format("No such keyword argument: %s", it->first));

          if (!result.specified.insert(std::make_pair(position->second, std::make_pair(it->second, SourceLocation()))).second)
            throw std::runtime_error(format("Multiple arguments passed to position %d", position->second));
        }

        /*
         * index_map maps argument indices to the partially applied
         * function to argument indices to the previous function.
         */
        std::vector<Maybe<unsigned> > index_map(function_description.argument_count);
        int index = 0;
        for (unsigned i = 0; i < function_description.argument_count; i++) {
          if (result.specified.find(i) == result.specified.end())
            index_map[i] = index++;
        }

        for (auto it = function_description.positional.begin(); it != function_description.positional.end(); ++it) {
            if (index_map[*it])
              result.description.positional.push_back(*index_map[*it]);
        }

        for (auto it = function_description.keywords.begin(); it != function_description.keywords.end(); ++it) {
          if (index_map[it->second])
              result.description.keywords.insert(std::make_pair(it->first, *index_map[it->second]));
        }

        return result;
      }

      CodeValue apply_function_call(const Value& function_value, const FunctionDescription& function_description,
                                    const SourceLocation& source,
                                    const std::unordered_map<unsigned, Value>& positional_arguments,
                                    const std::unordered_map<std::string, Value>& keyword_arguments) {
        
        auto common = apply_function_common(function_description, positional_arguments, keyword_arguments);

        CodeBlock code;
        std::vector<Value> argument_values;

        for (unsigned i = 0; i < function_description.argument_count; i++) {
          auto specified = common.specified.find(i);
          if (specified != common.specified.end()) {            
            argument_values.push_back(std::move(specified->second.first));
          } else {
            throw compile_error("default arguments not yet implemented");
          }
        }

        Instruction insn = call_instruction(function_value, argument_values);
        Value result = insn.value();
        code.append(std::move(insn));

        return {std::move(result), std::move(code)};
      }

      class FunctionMember : public MemberType {
      public:
        virtual LookupResult<EvaluateCallback> evaluate(PointerList<const Parser::Expression> arguments) const {
          return [=] (const Value& value, const EvaluateContext& context, const SourceLocation& location) -> CodeValue {
            auto& parameters = validate_call_arguments(arguments);
            auto result = compile_call_arguments(parameters, context, location.logical);
            auto call_result = apply_function_call(value, m_description, location, result.positional, result.keywords);

            return {std::move(call_result.value), {std::move(result.code), std::move(call_result.code)}};
          };
        }

      private:
        FunctionDescription m_description;
      };
    }
  }
}
