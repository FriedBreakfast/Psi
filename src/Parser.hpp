#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <memory>
#include <ostream>
#include <vector>

#include "Maybe.hpp"

namespace Psi {
  namespace Parser {
    class SourceTextReference;

    /**
     * \brief Text iterator.
     *
     * Wraps an underlying iterator to provide row and column number
     * tracking.
     */
    template<typename BaseIterator,
             typename CharTraits=std::char_traits<typename std::iterator_traits<BaseIterator>::value_type> >
    class TextIterator {
    public:
      typedef BaseIterator BaseIteratorType;
      typedef TextIterator<BaseIteratorType> this_type;
      typedef std::iterator_traits<BaseIterator> BaseTraitsType;
      typedef std::bidirectional_iterator_tag iterator_category;
      typedef typename BaseTraitsType::value_type value_type;
      typedef typename BaseTraitsType::difference_type difference_type;
      typedef typename BaseTraitsType::pointer pointer;
      typedef typename BaseTraitsType::reference reference;

      TextIterator()
        : m_line(0), m_column(0) {
      }

      TextIterator(int line, int column, const BaseIteratorType& base)
        : m_line(line), m_column(column), m_base(base) {
      }

      this_type& operator ++ () {
        if (!is_newline(*m_base)) {
          ++m_column;
        } else {
          m_column = 1;
          ++m_line;
        }
        ++m_base;
        return *this;
      }

      this_type operator ++ (int) const {
        this_type other(*this);
        ++other;
        return other;
      }

      this_type& operator -- () {
        --m_base;
        if (!is_newline(*m_base)) {
          --m_column;
        } else {
          m_column = 1;
          --m_line;
        }
        return *this;
      }

      this_type operator -- (int) const {
        this_type other(*this);
        --other;
        return other;
      }

      bool operator == (const this_type& other) const {
        return m_base == other.m_base;
      }

      bool operator != (const this_type& other) const {
        return m_base != other.m_base;
      }

      const value_type& operator * () const {
        return *m_base;
      }

      const value_type* operator -> () const {
        return m_base;
      }

      int line() const {
        return m_line;
      }

      int column() const {
        return m_column;
      }

      const BaseIteratorType& base() const {
        return m_base;
      }

    private:
      int m_line, m_column;
      BaseIteratorType m_base;

      static bool is_newline(const value_type& c) {
        return c == '\n';
      }
    };

    class PhysicalSourceLocation {
    public:
      typedef TextIterator<const char*> IteratorType;

      PhysicalSourceLocation() {
      }

      PhysicalSourceLocation(const std::shared_ptr<const void>& text_ptr_,
                      const IteratorType& begin_,
                      const IteratorType& end_)
        : begin(begin_),
          end(end_),
          text_ptr(text_ptr_) {
      }

      PhysicalSourceLocation(const std::shared_ptr<const std::vector<char> >& text_ptr_)
        : begin(1, 1, &*text_ptr_->begin()),
          end(-1, -1, &*text_ptr_->end()),
          text_ptr(text_ptr_) {
      }

      IteratorType begin;
      IteratorType end;
      std::shared_ptr<const void> text_ptr;

      std::string str() const {
        return std::string(begin, end);
      }

      friend std::ostream& operator << (std::ostream& os, const PhysicalSourceLocation& self) {
        std::copy(self.begin, self.end, std::ostreambuf_iterator<char>(os));
        return os;
      }
    };

    class ParseElement {
    public:
      ParseElement(const PhysicalSourceLocation& text)
        : m_text(text) {
      }

      const PhysicalSourceLocation& text() const {
        return m_text;
      }

    private:
      PhysicalSourceLocation m_text;
    };

    class Identifier : public ParseElement {
    public:
      Identifier(const PhysicalSourceLocation& text)
        : ParseElement(text) {
      }

      std::string str() const {
        return text().str();
      }
    };

    class MacroExpression;
    class TokenExpression;

    enum class ExpressionType {
      macro,
      token
    };

    class Expression : public ParseElement {
    public:
      Expression(const PhysicalSourceLocation& text,
                 ExpressionType type)
        : ParseElement(text),
          m_type(type) {
      }

      virtual ~Expression() = 0;

      ExpressionType type() const {
        return m_type;
      }

      const TokenExpression& as_token() const;
      const MacroExpression& as_macro() const;

    private:
      ExpressionType m_type;
    };

    class MacroExpression : public Expression {
    public:
      typedef Expression value_type;

      MacroExpression(const PhysicalSourceLocation& text,
		      std::vector<Expression> elements)
        : Expression(text, ExpressionType::macro),
          m_elements(std::move(elements)) {
      }

      const std::vector<value_type>& elements() const {
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

    class TokenExpression : public Expression {
    public:
      TokenExpression(PhysicalSourceLocation source, TokenType type, PhysicalSourceLocation text)
        : Expression(std::move(source), std::move(type)),
          m_text(std::move(text)) {
      }

      TokenExpression(PhysicalSourceLocation source, TokenType type, std::string literal)
        : Expression(std::move(source), std::move(type)),
          m_literal(std::move(literal)) {
      }

    private:
      PhysicalSourceLocation m_text;
      std::string m_literal;
    };

    class BadExpressionCast : public std::exception {
    public:
      virtual const char* what() throw() {
        return "Invalid Expression cast";
      }
    };

    inline const MacroExpression& Expression::as_macro() const {
      if (m_type == ExpressionType::macro)
        return *static_cast<const MacroExpression*>(this);
      else
        throw BadExpressionCast();
    }

    inline const UnaryExpression& Expression::as_unary() const {
      if (m_type == ExpressionType::unary)
        return *static_cast<const UnaryExpression*>(this);
      else
        throw BadExpressionCast();
    }

    inline const BinaryExpression& Expression::as_binary() const {
      if (m_type == ExpressionType::binary)
        return *static_cast<const BinaryExpression*>(this);
      else
        throw BadExpressionCast();
    }

    class Statement : public ParseElement {
    public:
      Statement(const PhysicalSourceLocation& text,
                const Identifier& identifier,
                const std::shared_ptr<const Expression>& expression)
        : ParseElement(text),
          m_identifier(identifier),
          m_expression(expression) {
      }

      Statement(const PhysicalSourceLocation& text,
                const std::shared_ptr<const Expression>& expression)
        : ParseElement(text),
          m_expression(expression) {
      }

      const std::shared_ptr<const Expression>& expression() const {
        return m_expression;
      }

      const Maybe<Identifier>& identifier() const {
        return m_identifier;
      }

    private:
      Maybe<Identifier> m_identifier;
      std::shared_ptr<const Expression> m_expression;
    };

    class ArgumentDeclaration : public ParseElement {
    public:
      ArgumentDeclaration(const PhysicalSourceLocation& text,
                           const Identifier& identifier)
        : ParseElement(text),
          m_identifier(identifier) {
      }

      ArgumentDeclaration(const PhysicalSourceLocation& text,
                           const Identifier& identifier,
                           const std::shared_ptr<const Expression>& expression)
        : ParseElement(text),
          m_identifier(identifier),
          m_expression(expression) {
      }

      const Identifier& identifier() const {
        return m_identifier;
      }

      const std::shared_ptr<const Expression>& expression() const {
        return m_expression;
      }

    private:
      Identifier m_identifier;
      std::shared_ptr<const Expression> m_expression;
    };

    class FunctionArgumentsDeclaration : public ParseElement {
    public:
      FunctionArgumentsDeclaration(const PhysicalSourceLocation& text,
                                     const ArgumentDeclarationList& arguments,
                                     const std::shared_ptr<const Expression>& return_type)
        : ParseElement(text),
          m_arguments(arguments),
          m_return_type(return_type) {
      }

      const ArgumentDeclarationList& arguments() const {
        return m_arguments;
      }

      const std::shared_ptr<const Expression>& return_type() const {
        return m_return_type;
      }

    private:
      ArgumentDeclarationList m_arguments;
      std::shared_ptr<const Expression> m_return_type;
    };

    class ParseError : public std::runtime_exception {
    public:
      ParseError(const std::string& reason);
      virtual ~ParseError() throw();
    };

    /** \brief parse a statement list.
     * \param text Text to parse.
     * \return the resulting parse tree.
     */
    StatementList parse_statement_list(const PhysicalSourceLocation& text);

    /** \brief parse an argument list.
     * \details an argument list is a list of Expressions forming arguments to a function call.
     * \param text Text to parse.
     * \return the resulting parse tree.
     * \return the resulting StatementList.
     */
    ArgumentList parse_argument_list(const PhysicalSourceLocation& text);

    /** \brief parse a function argument declaration.
     * \details This is a list of argument declarations possibly
     * followed by a return type Expression.
     * \param text Text to parse.
     * \return the resulting parse tree.
     * \return the resulting StatementList.
     */
    FunctionArgumentsDeclaration parse_function_argument_declaration(const PhysicalSourceLocation& text);
  }
}

#endif
