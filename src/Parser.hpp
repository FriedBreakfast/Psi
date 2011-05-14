#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <stdexcept>
#include <vector>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"
#include "Runtime.hpp"

namespace Psi {
  namespace Parser {
    using Compiler::PhysicalSourceLocation;

    struct Element {
      Element(const PhysicalSourceLocation& location_);

      PhysicalSourceLocation location;
    };

    enum ExpressionType {
      expression_token,
      expression_macro,
      expression_dot
    };

    struct Expression : Element {
      Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_);
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

      TokenExpression(const PhysicalSourceLocation& location_, TokenType token_type_, const PhysicalSourceLocation& text_);
      virtual ~TokenExpression();

      TokenType token_type;
      PhysicalSourceLocation text;
    };

    class Expression;

    struct MacroExpression : Expression {
      MacroExpression(const PhysicalSourceLocation& location_, const std::vector<SharedPtr<Expression> >& elements_);
      virtual ~MacroExpression();

      std::vector<SharedPtr<Expression> > elements;
    };

    struct NamedExpression : Element {
      NamedExpression(const PhysicalSourceLocation& source_);
      NamedExpression(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& expression_);
      NamedExpression(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& expression_, const PhysicalSourceLocation& name_);
      ~NamedExpression();

      boost::optional<PhysicalSourceLocation> name;
      SharedPtr<Expression> expression;
    };

    struct DotExpression : Expression {
      DotExpression(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& left_, const SharedPtr<Expression>& right_);
      ~DotExpression();

      SharedPtr<Expression> left, right;
    };

    class ParseError : public std::runtime_error {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    std::vector<SharedPtr<NamedExpression> > parse_statement_list(const PhysicalSourceLocation&);
    std::vector<SharedPtr<NamedExpression> > parse_argument_list(const PhysicalSourceLocation&);

    struct ArgumentDeclarations {
      std::vector<SharedPtr<NamedExpression> > arguments;
      SharedPtr<Expression> return_type;
    };

    ArgumentDeclarations parse_function_argument_declarations(const PhysicalSourceLocation&);
  }
}

#endif
