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
    };

    class ParseElement {
    public:
      ParseElement(const PhysicalSourceLocation& text)
        : m_text(text) {
      }

      const PhysicalSourceLocation& source() const {
        return m_text;
      }

    private:
      PhysicalSourceLocation m_text;
    };

    class Expression;

    class MacroExpression {
    public:
      MacroExpression(std::vector<Expression> elements)
        : m_elements(std::move(elements)) {
      }

      MacroExpression(MacroExpression&&) = default;

      const std::vector<Expression>& elements() const {
        return m_elements;
      }

    private:
      std::vector<Expression> m_elements;
    };

    enum class TokenType {
      identifier,
      brace,
      square_bracket,
      bracket
    };

    class TokenExpression {
    public:
      TokenExpression(TokenType type, PhysicalSourceLocation text)
        : m_text(std::move(text)),
          m_token_type(std::move(type)) {
      }

      const PhysicalSourceLocation& text() const {return m_text;}
      TokenType token_type() const {return m_token_type;}

    private:
      PhysicalSourceLocation m_text;
      TokenType m_token_type;
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

      const TokenExpression& token() {return m_data.get<TokenExpression>();}
      const MacroExpression& macro() {return m_data.get<MacroExpression>();}

    private:
      Variant<TokenExpression, MacroExpression> m_data;
    };

    class Statement : public ParseElement {
    public:
      Statement(PhysicalSourceLocation text,
                Expression expression,
                PhysicalSourceLocation identifier)
        : ParseElement(std::move(text)),
          m_identifier(std::move(identifier)),
          m_expression(std::move(expression)) {
      }

      Statement(PhysicalSourceLocation text,
                Expression expression)
        : ParseElement(std::move(text)),
          m_expression(std::move(expression)) {
      }

      Statement(PhysicalSourceLocation text)
        : ParseElement(std::move(text)) {
      }

      const Maybe<Expression>& expression() const {
        return m_expression;
      }

      const Maybe<PhysicalSourceLocation>& identifier() const {
        return m_identifier;
      }

    private:
      Maybe<PhysicalSourceLocation> m_identifier;
      Maybe<Expression> m_expression;
    };

    class ArgumentDeclaration : public ParseElement {
    public:
      ArgumentDeclaration(PhysicalSourceLocation text,
                          PhysicalSourceLocation identifier)
        : ParseElement(std::move(text)),
          m_identifier(std::move(identifier)) {
      }

      ArgumentDeclaration(PhysicalSourceLocation text,
                          PhysicalSourceLocation identifier,
                          Expression expression)
        : ParseElement(std::move(text)),
          m_identifier(std::move(identifier)),
          m_expression(std::move(expression)) {
      }

      const PhysicalSourceLocation& identifier() const {
        return m_identifier;
      }

      const Maybe<Expression>& expression() const {
        return m_expression;
      }

    private:
      PhysicalSourceLocation m_identifier;
      Maybe<Expression> m_expression;
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
    std::vector<Expression> parse_argument_list(const PhysicalSourceLocation& text);

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
