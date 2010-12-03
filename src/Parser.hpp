#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include "Utility.hpp"

#include <stdexcept>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

namespace Psi {
  namespace Parser {
    struct Location {
      const char *begin;
      const char *end;

      int first_line;
      int first_column;
      int last_line;
      int last_column;
    };

    struct Element {
      Element(const Location& location_);

      Location location;
    };

    enum ExpressionType {
      expression_token,
      expression_macro
    };

    struct Expression : Element, boost::intrusive::list_base_hook<> {
      Expression(const Location& location_, ExpressionType expression_type_);
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

      TokenExpression(const Location& location_, TokenType token_type_, const Location& text_);
      virtual ~TokenExpression();

      TokenType token_type;
      Location text;
    };

    class Expression;

    struct MacroExpression : Expression {
      MacroExpression(const Location& location_, UniqueList<Expression>& elements_);
      virtual ~MacroExpression();

      UniqueList<Expression> elements;
    };

    struct NamedExpression : Element, boost::intrusive::list_base_hook<> {
      NamedExpression(const Location& source_);
      NamedExpression(const Location& source_, UniquePtr<Expression>& expression_);
      NamedExpression(const Location& source_, UniquePtr<Expression>& expression_, const Location& name_);
      ~NamedExpression();

      UniquePtr<Expression> expression;
      boost::optional<Location> name;
    };

    class ParseError : public std::runtime_error {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    /** \brief parse a statement list.
     * \param text Text to parse.
     */
    void parse_statement_list(const char *begin, const char *end, UniqueList<NamedExpression>& result);

    /** \brief parse an argument list.
     * \details an argument list is a list of Expressions forming arguments to a function call.
     * \param text Text to parse.
     */
    void parse_argument_list(const char *begin, const char *end, UniqueList<NamedExpression>& result);

    /** \brief parse a function argument declaration.
     * \details This is a list of argument declarations possibly
     * followed by a return type Expression.
     * \param text Text to parse.
     */
    void parse_argument_declarations(const char *begin, const char *end, UniqueList<NamedExpression>& result);
  }
}

#endif
