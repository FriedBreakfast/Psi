#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <stdexcept>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "Runtime.hpp"

namespace Psi {
  namespace Parser {
    struct ParserLocation {
      Compiler::PhysicalSourceLocation location;
      const char *begin, *end;
    };

    struct Element {
      Element(const ParserLocation& location_);

      ParserLocation location;
    };

    enum ExpressionType {
      expression_token,
      expression_macro,
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
	brace,
	square_bracket,
	bracket
      };

      TokenExpression(const ParserLocation& location_, TokenType token_type_, const ParserLocation& text_);
      virtual ~TokenExpression();

      TokenType token_type;
      ParserLocation text;
    };

    class Expression;

    struct MacroExpression : Expression {
      MacroExpression(const ParserLocation& location_, const ArrayList<SharedPtr<Expression> >& elements_);
      virtual ~MacroExpression();

      ArrayList<SharedPtr<Expression> > elements;
    };

    struct NamedExpression : Element {
      NamedExpression(const ParserLocation& source_);
      NamedExpression(const ParserLocation& source_, const SharedPtr<Expression>& expression_);
      NamedExpression(const ParserLocation& source_, const SharedPtr<Expression>& expression_, const ParserLocation& name_);
      ~NamedExpression();

      boost::optional<ParserLocation> name;
      SharedPtr<Expression> expression;
    };

    struct DotExpression : Expression {
      DotExpression(const ParserLocation& source_, const SharedPtr<Expression>& left_, const SharedPtr<Expression>& right_);
      ~DotExpression();

      SharedPtr<Expression> left, right;
    };

    class ParseError : public std::runtime_error {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    ArrayList<SharedPtr<NamedExpression> > parse_statement_list(const ParserLocation&);
    ArrayList<SharedPtr<NamedExpression> > parse_argument_list(const ParserLocation&);

    struct ArgumentDeclarations {
      ArrayList<SharedPtr<NamedExpression> > arguments;
      SharedPtr<Expression> return_type;
    };

    ArgumentDeclarations parse_function_argument_declarations(const ParserLocation&);
  }
}

#endif
