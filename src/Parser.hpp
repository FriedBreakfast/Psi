#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <stdexcept>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "SourceLocation.hpp"
#include "Runtime.hpp"

namespace Psi {
  namespace Parser {
    struct ParserLocation {
      PhysicalSourceLocation location;
      const char *begin, *end;
    };

    struct Element {
      Element(const ParserLocation& location_);

      ParserLocation location;
    };

    enum ExpressionType {
      expression_token,
      expression_evaluate,
      expression_dot
    };

    struct Expression : Element {
      Expression(const ParserLocation& location_, ExpressionType expression_type_);
      virtual ~Expression();

      ExpressionType expression_type;
    };

    struct TokenExpression : Expression {
      enum TokenType {
	identifier,
        number,
	brace,
	square_bracket,
	bracket
      };

      TokenExpression(const ParserLocation& location_, TokenType token_type_, const ParserLocation& text_);
      virtual ~TokenExpression();

      TokenType token_type;
      ParserLocation text;
    };

    struct EvaluateExpression : Expression {
      EvaluateExpression(const ParserLocation& location_, const SharedPtr<Expression>& object_, const PSI_STD::vector<SharedPtr<Expression> >& parameters_);
      virtual ~EvaluateExpression();

      SharedPtr<Expression> object;
      PSI_STD::vector<SharedPtr<Expression> > parameters;
    };
    
    struct DotExpression : Expression {
      DotExpression(const ParserLocation& source_, const SharedPtr<Expression>& obj_, const SharedPtr<Expression>& member_, const PSI_STD::vector<SharedPtr<Expression> >& parameters_);
      virtual ~DotExpression();
      
      SharedPtr<Expression> object;
      SharedPtr<Expression> member;
      PSI_STD::vector<SharedPtr<Expression> > parameters;
    };

    struct NamedExpression : Element {
      NamedExpression(const ParserLocation& source_);
      NamedExpression(const ParserLocation& source_, const SharedPtr<Expression>& expression_);
      NamedExpression(const ParserLocation& source_, const SharedPtr<Expression>& expression_, const ParserLocation& name_, bool functional_);
      ~NamedExpression();

      boost::optional<ParserLocation> name;
      bool functional;
      SharedPtr<Expression> expression;
    };
    
    struct FunctionArgument : Element {
      FunctionArgument(const ParserLocation& source_, const boost::optional<ParserLocation>& name_,
                       const boost::optional<ParserLocation>& mode_, const SharedPtr<Expression>& type_);

      boost::optional<ParserLocation> name;
      boost::optional<ParserLocation> mode;
      SharedPtr<Expression> type;
    };

    class ParseError : public std::runtime_error {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    PSI_STD::vector<SharedPtr<NamedExpression> > parse_statement_list(const ParserLocation&);
    PSI_STD::vector<SharedPtr<NamedExpression> > parse_argument_list(const ParserLocation&);
    PSI_STD::vector<SharedPtr<Expression> > parse_positional_list(const ParserLocation&);
    SharedPtr<Expression> parse_expression(const ParserLocation& text);
    PSI_STD::vector<TokenExpression> parse_identifier_list(const ParserLocation&);

    struct ImplicitArgumentDeclarations {
      PSI_STD::vector<SharedPtr<NamedExpression> > arguments;
      PSI_STD::vector<SharedPtr<Expression> > interfaces;
    };
    
    struct ArgumentDeclarations {
      PSI_STD::vector<SharedPtr<FunctionArgument> > arguments;
      SharedPtr<FunctionArgument> return_type;
    };

    ArgumentDeclarations parse_function_argument_declarations(const ParserLocation&);
    ImplicitArgumentDeclarations parse_function_argument_implicit_declarations(const ParserLocation& text);
    SharedPtr<TokenExpression> expression_as_token_type(const SharedPtr<Expression>& expr, TokenExpression::TokenType type);
  }
}

#endif
