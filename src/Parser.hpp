#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <memory>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "Maybe.hpp"
#include "Variant.hpp"

namespace Psi {
  namespace Parser {
    class SourceCodeOrigin {
    };

    struct SourceCodeText {
      std::shared_ptr<SourceCodeOrigin> origin;
      std::shared_ptr<void> data;
    };

    struct PhysicalSourceLocation {
      std::shared_ptr<SourceCodeText> text;
      int first_line, first_column, last_line, last_column;
      const char *begin, *end;

      PhysicalSourceLocation() : first_line(0), first_column(0), last_line(0), last_column(0), begin(0), end(0) {}

      std::string str() const {return std::string(begin, end-begin);}
    };

    struct ParseElement {
      ParseElement(ParseElement&&) = default;
      ParseElement(PhysicalSourceLocation source_) : source(std::move(source_)) {}

      PhysicalSourceLocation source;
    };

    class Expression;

    struct MacroExpression {
      std::vector<Expression> elements;
    };

    enum class TokenType {
      identifier,
      brace,
      square_bracket,
      bracket
    };

    struct TokenExpression {
      TokenType token_type;
      PhysicalSourceLocation text;
    };

    enum class ExpressionType {
      token,
      macro
    };

    class Expression : public ParseElement {
    public:
      typedef Variant<TokenExpression, MacroExpression> DataType;

      Expression(PhysicalSourceLocation text, TokenExpression token)
        : ParseElement(std::move(text)), m_data(std::move(token)) {
      }

      Expression(PhysicalSourceLocation text, MacroExpression macro)
        : ParseElement(std::move(text)), m_data(std::move(macro)) {
      }

      Expression(Expression&&) = default;

      ExpressionType which() const {
        switch (m_data.which()) {
        case 1: return ExpressionType::token;
        case 2: return ExpressionType::macro;
        default: throw std::logic_error("expression data is not valid");
        }
      }

      const TokenExpression& token() const {return m_data.get<TokenExpression>();}
      const MacroExpression& macro() const {return m_data.get<MacroExpression>();}

    private:
      Variant<TokenExpression, MacroExpression> m_data;
    };

    struct Statement : ParseElement {
      Statement(PhysicalSourceLocation source_) : ParseElement(std::move(source_)) {}
      Statement(PhysicalSourceLocation source_, Expression expression_) : ParseElement(std::move(source_)), expression(std::move(expression_)) {}
      Statement(PhysicalSourceLocation source_, Expression expression_, PhysicalSourceLocation name_) : ParseElement(std::move(source_)), expression(std::move(expression_)), name(std::move(name_)) {}
      Statement(Statement&&) = default;

      Maybe<Expression> expression;
      Maybe<PhysicalSourceLocation> name;
    };

    struct ArgumentDeclaration : ParseElement {
      ArgumentDeclaration(PhysicalSourceLocation source_, PhysicalSourceLocation name_) : ParseElement(std::move(source_)), name(std::move(name_)) {}
      ArgumentDeclaration(PhysicalSourceLocation source_, PhysicalSourceLocation name_, Expression expression_) : ParseElement(std::move(source_)), name(std::move(name_)), expression(std::move(expression_)) {}
      ArgumentDeclaration(ArgumentDeclaration&&) = default;

      PhysicalSourceLocation name;
      Maybe<Expression> expression;
    };

    struct Argument : ParseElement {
      Argument(PhysicalSourceLocation source_, Expression value_, PhysicalSourceLocation name_) : ParseElement(std::move(source_)), value(std::move(value_)), name(std::move(name_)) {}
      Argument(PhysicalSourceLocation source_, Expression value_) : ParseElement(std::move(source_)), value(std::move(value_)) {}
      Argument(Argument&&) = default;

      Expression value;
      Maybe<PhysicalSourceLocation> name;
    };

    class ParseError : public std::runtime_error {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    /** \brief parse a statement list.
     * \param text Text to parse.
     * \return the resulting parse tree.
     */
    std::vector<Statement> parse_statement_list(const PhysicalSourceLocation& text);

    /** \brief parse an argument list.
     * \details an argument list is a list of Expressions forming arguments to a function call.
     * \param text Text to parse.
     * \return the resulting parse tree.
     * \return the resulting StatementList.
     */
    std::vector<Argument> parse_argument_list(const PhysicalSourceLocation& text);

    /** \brief parse a function argument declaration.
     * \details This is a list of argument declarations possibly
     * followed by a return type Expression.
     * \param text Text to parse.
     * \return the resulting parse tree.
     * \return the resulting StatementList.
     */
    std::vector<ArgumentDeclaration> parse_argument_declarations(const PhysicalSourceLocation& text);
  }
}

#endif
