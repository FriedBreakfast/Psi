#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <stdexcept>
#include <vector>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "Compiler.hpp"

namespace Psi {
  namespace Parser {
    using Compiler::PhysicalSourceLocation;

    struct Element {
      Element(const PhysicalSourceLocation& location_);

      PhysicalSourceLocation location;
    };

    enum ExpressionType {
      expression_token,
      expression_macro
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
      MacroExpression(const PhysicalSourceLocation& location_, const std::vector<boost::shared_ptr<Expression> >& elements_);
      virtual ~MacroExpression();

      std::vector<boost::shared_ptr<Expression> > elements;
    };

    struct NamedExpression : Element {
      NamedExpression(const PhysicalSourceLocation& source_);
      NamedExpression(const PhysicalSourceLocation& source_, const boost::shared_ptr<Expression>& expression_);
      NamedExpression(const PhysicalSourceLocation& source_, const boost::shared_ptr<Expression>& expression_, const PhysicalSourceLocation& name_);
      ~NamedExpression();

      boost::optional<PhysicalSourceLocation> name;
      boost::shared_ptr<Expression> expression;
    };

    class ParseError : public std::runtime_error {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    /** \brief parse a statement list.
     * \param text Text to parse.
     */
    std::vector<boost::shared_ptr<NamedExpression> > parse_statement_list(const char *begin, const char *end);

    /** \brief parse an argument list.
     * \details an argument list is a list of Expressions forming arguments to a function call.
     * \param text Text to parse.
     */
    std::vector<boost::shared_ptr<NamedExpression> > parse_argument_list(const char *begin, const char *end);

    /** \brief parse a function argument declaration.
     * \details This is a list of argument declarations possibly
     * followed by a return type Expression.
     * \param text Text to parse.
     */
    std::vector<boost::shared_ptr<NamedExpression> > parse_argument_declarations(const char *begin, const char *end);
  }
}

#endif
