#include "Parser.hpp"
#include "Lexer.hpp"

#include <boost/format.hpp>

namespace Psi {
namespace Parser {
Text::Text()
: begin(NULL), end(NULL) {
}

Text::Text(const PhysicalSourceLocation& location_, const Psi::SharedPtrHandle& data_handle_, const char *begin_, const char *end_)
: location(location_), data_handle(data_handle_), begin(begin_), end(end_) {
}

String Text::str() const {
  return String(begin, end);
}

Element::Element(const PhysicalSourceLocation& location_)
: location(location_) {
}

Expression::Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_)
: Element(location_),
expression_type(expression_type_) {
}

Expression::~Expression() {
}

TokenExpression::TokenExpression(const PhysicalSourceLocation& location_, TokenExpressionType token_type_, const Text& text_)
: Expression(location_, expression_token),
token_type(token_type_),
text(text_) {
}

TokenExpression::~TokenExpression() {
}

EvaluateExpression::EvaluateExpression(const PhysicalSourceLocation& location_, const SharedPtr<Expression>& object_, const PSI_STD::vector<SharedPtr<Expression> >& parameters_)
: Expression(location_, expression_evaluate), object(object_), parameters(parameters_) {
}

EvaluateExpression::~EvaluateExpression() {
}

DotExpression::DotExpression(const PhysicalSourceLocation& location_, const SharedPtr<Expression>& object_,
                             const SharedPtr<Expression>& member_, const PSI_STD::vector<SharedPtr<Expression> >& parameters_)
: Expression(location_, expression_dot), object(object_), member(member_), parameters(parameters_) {
}

DotExpression::~DotExpression() {
}

Statement::Statement(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& expression_, const Maybe<Text>& name_, int mode_)
: Element(source_), name(name_), mode(mode_), expression(expression_) {
}

Statement::~Statement() {
}

Implementation::Implementation(const PhysicalSourceLocation& source_, bool constructor_, const SharedPtr<Expression>& interface_, const SharedPtr<Expression>& arguments_, const SharedPtr<Expression>& value_)
: Element(source_), constructor(constructor_), interface(interface_), arguments(arguments_), value(value_) {
}

Implementation::~Implementation() {
}

Lifecycle::Lifecycle(const PhysicalSourceLocation& source_, const Text& function_name_, const Text& dest_name_,
                     const Maybe<Text>& src_name_, const SharedPtr<TokenExpression>& body_)
: Element(source_), function_name(function_name_), dest_name(dest_name_),
src_name(src_name_), body(body_) {
}

Lifecycle::~Lifecycle() {
}

FunctionArgument::FunctionArgument(const PhysicalSourceLocation& source_, const Maybe<Text>& name_,
                                   Compiler::ParameterMode mode_, const SharedPtr<Expression>& type_)
: Element(source_), is_interface(false), name(name_), mode(mode_), type(type_) {
}

FunctionArgument::FunctionArgument(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& interface_)
: Element(source_), is_interface(true), type(interface_) {
}

enum LongToken {
  tok_eof=256,
  tok_id,
  tok_number,
  tok_cmp_eq, ///< ==
  tok_cmp_ne, ///< !=
  tok_cmp_le, ///< <=
  tok_cmp_ge, ///< >=
  
  tok_op_arrow, ///< ->
  tok_op_double_arrow, ///< =>
  tok_op_dash_colon, ///< -:
  tok_op_dash_amp, ///< -&
  tok_op_dash_amp_amp, ///< -&&
  tok_op_double_colon, ///< ::
  tok_op_colon_amp, ///< :&
  tok_op_colon_amp_amp, ///< :&&
  tok_op_colon_right, ///< :>
  
  tok_block_bracket,
  tok_block_square_bracket,
  tok_block_brace
};

class LexerImpl {
  SharedPtrHandle m_data_handle;
  
public:
  typedef LexerValue<int, SharedPtr<TokenExpression> > ValueType;

  explicit LexerImpl(const SharedPtrHandle& data_handle);
  ValueType lex(LexerPosition& pos);
  std::string error_name(int tok);
  std::string error_name(const ValueType& value);
};

LexerImpl::LexerImpl(const SharedPtrHandle& data_handle)
: m_data_handle(data_handle) {
}

std::string LexerImpl::error_name(int tok) {
  switch (tok) {
  case tok_eof: return "end-of-stream";
  case tok_id: return "identifier";
  case tok_number: return "number";
  case tok_block_bracket: return "(...)";
  case tok_block_square_bracket: return "[...]";
  case tok_block_brace: return "{...}";
  
  case tok_cmp_eq: return "==";
  case tok_cmp_ne: return "!=";
  case tok_cmp_le: return "<=";
  case tok_cmp_ge: return ">=";
  
  case tok_op_arrow: return "->";
  case tok_op_double_arrow: return "=>";
  case tok_op_dash_colon: return "-:";
  case tok_op_dash_amp: return "-&";
  case tok_op_dash_amp_amp: return "-&&";
  case tok_op_double_colon: return "-::";
  case tok_op_colon_amp: return ":&";
  case tok_op_colon_amp_amp: return ":&&";
  case tok_op_colon_right: return ":>";
  
  default: return std::string(1, char(tok));
  }
}

std::string LexerImpl::error_name(const LexerImpl::ValueType& value) {
  switch (value.id()) {
  case tok_id:
  case tok_number: return value.value()->text.str();
  default: return error_name(value.id());
  }
}

/**
 * \brief Scanner function.
 *
 * I've done this as a handwritten function rather than using Flex
 * because bending Flex to my will seems like slightly more effort
 * than doing this. I'd like it to be able to operate on a buffer
 * without altering it, which this function does, and it takes less
 * code than Flex and the myriad of options required to make a Flex
 * scanner sensible.
 *
 * \note Note that this function lives here rather than above the
 * main parser body (where it is declared) because it required the
 * parser token definitions.
 */
LexerImpl::ValueType LexerImpl::lex(LexerPosition& pos) {
  pos.skip_whitespace();
  
  if (pos.end())
    return ValueType(tok_eof, pos.location());

  if (c_isdigit(pos.current())) {
    bool has_dot = false;
    pos.accept();

    while (!pos.end()) {
      if (pos.current() == '.') {
        if (has_dot) {
          break;
        } else {
          has_dot = true;
          pos.accept();
        }
      } else if (c_isalnum(pos.current())) {
        pos.accept();
      } else {
        break;
      }
    }

    SharedPtr<TokenExpression> expr(new TokenExpression(pos.location(), token_number,
                                                        Text(pos.location(), m_data_handle, pos.token_start(), pos.token_end())));

    return ValueType(tok_number, pos.location(), expr);
  } else if (c_isalpha(pos.current()) || (pos.current() == '_')) {
    pos.accept();
    while (!pos.end()) {
      if (c_isalnum(pos.current()) || (pos.current() == '_'))
        pos.accept();
      else
        break;
    }
    
    SharedPtr<TokenExpression> expr(new TokenExpression(pos.location(), token_identifier,
                                                        Text(pos.location(), m_data_handle, pos.token_start(), pos.token_end())));
    
    return ValueType(tok_id, pos.location(), expr);
  } else if (std::strchr("<>=!", pos.current())) {
    /* Multi-char comparison operators and the double arrow operator */
    char c = pos.current();
    pos.accept();
    if (!pos.end()) {
      switch (pos.current()) {
      case '=':
        switch(c) {
        case '>': pos.accept(); return ValueType(tok_cmp_ge, pos.location());
        case '<': pos.accept(); return ValueType(tok_cmp_le, pos.location());
        case '=': pos.accept(); return ValueType(tok_cmp_eq, pos.location());
        case '!': pos.accept(); return ValueType(tok_cmp_ne, pos.location());
        default: break;
        }
        
      case '>':
        switch(c) {
        case '=': pos.accept(); return ValueType(tok_op_double_arrow, pos.location());
        default: break;
        }

      default:
        break;
      }
    }

    return ValueType(c, pos.location());
  } else if (pos.current() == '-') {
    pos.accept();
    if (!pos.end()) {
      switch (pos.current()) {
      case '>': pos.accept(); return ValueType(tok_op_arrow, pos.location());
      case ':': pos.accept(); return ValueType(tok_op_dash_colon, pos.location());
      case '&': {
        pos.accept();
        if (!pos.end() && (pos.current() == '&')) {
          pos.accept();
          return ValueType(tok_op_dash_amp_amp, pos.location());
        }
        return ValueType(tok_op_dash_amp, pos.location());
      }
      
      default: break;
      }
    }
    return ValueType('-', pos.location());
  } else if (pos.current() == ':') {
    /* Variable assignment operators */
    pos.accept();
    if (!pos.end()) {
      switch (pos.current()) {
      case ':': pos.accept(); return ValueType(tok_op_double_colon, pos.location());
      case '>': pos.accept(); return ValueType(tok_op_colon_right, pos.location());
      case '&': {
        pos.accept();
        if (!pos.end() && (pos.current() == '&')) {
          pos.accept();
          return ValueType(tok_op_colon_amp_amp, pos.location());
        }
        return ValueType(tok_op_colon_amp, pos.location());
      }
      
      default: break;
      }
    }
    
    return ValueType(':', pos.location());
  } else if (std::strchr(".;,+*/%^&|", pos.current())) {
    char c = pos.current();
    pos.accept();
    return ValueType(c, pos.location());
  } else if (std::strchr("{[(", pos.current())) {
    int brace_depth = 0, square_bracket_depth = 0, bracket_depth = 0;
    TokenExpressionType block_type;

    int token_type;
    switch(pos.current()) {
    case '(': token_type = tok_block_bracket; block_type = token_bracket; break;
    case '[': token_type = tok_block_square_bracket; block_type = token_square_bracket; break;
    case '{': token_type = tok_block_brace; block_type = token_brace; break;
    default: PSI_FAIL("should not reach this point");
    }

    while (true) {
      char c = pos.current();
      pos.accept();

      if (c == '\\') {
        if (pos.end())
          pos.error(pos.location(), "End-of-stream following '\\' whilst scanning bracket group");
        pos.accept();
      } else if (c == '{') {
        ++brace_depth;
      } else if (c == '}') {
        if (brace_depth == 0)
          pos.error(pos.location(), "Closing '}' without previous opening '{'");
        --brace_depth;
      } else if (brace_depth == 0) {
        if (c == '[') {
          ++square_bracket_depth;
        } else if (c == ']') {
          if (square_bracket_depth == 0)
            pos.error(pos.location(), "Closing ']' without previous opening '['");
          --square_bracket_depth;
        } else if (square_bracket_depth == 0) {
          if (c == '(') {
            ++bracket_depth;
          } else if (c == ')') {
            if (bracket_depth == 0)
              pos.error(pos.location(), "Closing ')' without previous opening '('");
            --bracket_depth;
          }
        }
      }

      if ((bracket_depth == 0) && (square_bracket_depth == 0) && (brace_depth == 0)) {
        PhysicalSourceLocation text_location = pos.location();
        ++text_location.first_column;
        --text_location.last_column;

        SharedPtr<TokenExpression> expr(new TokenExpression(pos.location(), block_type,
                                                            Text(text_location, m_data_handle, pos.token_start() + 1, pos.token_end() - 1)));

        return ValueType(token_type, pos.location(), expr);
      } else if (pos.end()) {
        pos.error(pos.location(), "Unexpected end-of-stream whilst scanning bracket group");
      }
    }
  } else {
    char c = pos.current();
    pos.accept();
    return ValueType(c, pos.location());
  }
}

class ParserImpl {
public:
  typedef Lexer<2, int, SharedPtr<TokenExpression>, LexerImpl> LexerType;
  
  ParserImpl(LexerType *lexer) : m_lexer(lexer) {}
  LexerType& lex() {return *m_lexer;}
  
  static SharedPtr<Expression> str_expression(const PhysicalSourceLocation& loc, const char *op);
  static SharedPtr<Expression> binary_expr(const PhysicalSourceLocation& origin, const PhysicalSourceLocation& op_loc, const char *op, const SharedPtr<Expression>& lhs, const SharedPtr<Expression>& rhs);
  static SharedPtr<Expression> unary_expr(const PhysicalSourceLocation& origin, const PhysicalSourceLocation& op_loc, const char *op, const SharedPtr<Expression>& param);

  PSI_STD::vector<SharedPtr<Statement> > parse_statement_list();
  SharedPtr<Statement> parse_statement();
  
  PSI_STD::vector<SharedPtr<Statement> > parse_namespace();
  SharedPtr<Statement> parse_namespace_entry();

  PSI_STD::vector<SharedPtr<Expression> > parse_positional_list();
  
  SharedPtr<Expression> parse_expression();
  SharedPtr<Expression> parse_or_expression();
  SharedPtr<Expression> parse_xor_expression();
  SharedPtr<Expression> parse_and_expression();
  SharedPtr<Expression> parse_compare_expression();
  SharedPtr<Expression> parse_sum_expression();
  SharedPtr<Expression> parse_product_expression();
  SharedPtr<Expression> parse_unary_expression();
  SharedPtr<Expression> parse_macro_expression();
  SharedPtr<TokenExpression> parse_token_expression();
  PSI_STD::vector<SharedPtr<Expression> > parse_token_list();
  
  PSI_STD::vector<TokenExpression> parse_identifier_list();

  PSI_STD::vector<SharedPtr<FunctionArgument> > parse_argument_list_declare();
  SharedPtr<FunctionArgument> parse_argument_declare();
  Maybe<Compiler::ResultMode> parse_result_mode();

  FunctionArgumentDeclarations parse_function_argument_declarations();
  ImplementationArgumentDeclaration parse_implementation_arguments();
  
private:
  LexerType *m_lexer;
  
  const char* op_callback_assign();
  const char* op_callback_or();
  const char* op_callback_xor();
  const char* op_callback_and();
  const char* op_callback_sum();
  const char* op_callback_product();
  const char* op_callback_compare();
  SharedPtr<Expression> parse_binary_expression(const char* (ParserImpl::*op_callback) (),
                                                SharedPtr<Expression> (ParserImpl::*child) ());
};

SharedPtr<Expression> ParserImpl::str_expression(const PhysicalSourceLocation& loc, const char *op) {
  Text text;
  text.location = loc;
  text.begin = op;
  text.end = op + std::strlen(op);

  return SharedPtr<Expression>(new TokenExpression(loc, token_identifier, text));
}

SharedPtr<Expression> ParserImpl::binary_expr(const PhysicalSourceLocation& origin,
                                              const PhysicalSourceLocation& op_loc, const char *op,
                                              const SharedPtr<Expression>& lhs,
                                              const SharedPtr<Expression>& rhs) {
  PSI_STD::vector<SharedPtr<Expression> > args;
  args.push_back(lhs);
  args.push_back(rhs);

  return SharedPtr<Expression>(new EvaluateExpression(origin, str_expression(op_loc, op), args));
}

SharedPtr<Expression> ParserImpl::unary_expr(const PhysicalSourceLocation& origin,
                                             const PhysicalSourceLocation& op_loc, const char *op,
                                             const SharedPtr<Expression>& param) {
  PSI_STD::vector<SharedPtr<Expression> > args;
  args.push_back(param);

  return SharedPtr<Expression>(new EvaluateExpression(origin, str_expression(op_loc, op), args));
}

PSI_STD::vector<SharedPtr<Statement> > ParserImpl::parse_statement_list() {
  PSI_STD::vector<SharedPtr<Statement> > result;
  do {
    result.push_back(parse_statement());
  } while (lex().accept(';'));
  return result;
}

SharedPtr<Statement> ParserImpl::parse_statement() {
  PhysicalSourceLocation loc = lex().loc_begin();

  if (lex().accept(tok_eof))
    return SharedPtr<Statement>();
  
  bool has_id = lex().accept(tok_id);
  
  Compiler::StatementMode mode;
  if (lex().accept(':'))
    mode = Compiler::statement_mode_value;
  else if (lex().accept(tok_op_double_colon))
    mode = Compiler::statement_mode_functional;
  else if (lex().accept(tok_op_colon_amp))
    mode = Compiler::statement_mode_ref;
  else {
    mode = Compiler::statement_mode_destroy;
    if (has_id) {
      // Can't have an identifier without a mode
      lex().back();
      has_id = false;
    }
  }
  
  Maybe<Text> identifier;
  if (has_id)
    identifier = lex().value(1).value()->text;
  
  SharedPtr<Expression> expr = parse_expression();
  lex().loc_end(loc);
  
  return SharedPtr<Statement>(new Statement(loc, expr, identifier, mode));
}

/**
 * \brief parse a statement list.
 * \param text Text to parse.
 */
PSI_STD::vector<SharedPtr<Statement> > parse_statement_list(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  PSI_STD::vector<SharedPtr<Statement> > result = ParserImpl(&lexer).parse_statement_list();
  lexer.expect(tok_eof);
  return result;
}

PSI_STD::vector<SharedPtr<Statement> > ParserImpl::parse_namespace() {
  PSI_STD::vector<SharedPtr<Statement> > result;
  do {
    result.push_back(parse_namespace_entry());
  } while (lex().accept(';'));
  return result;
}

SharedPtr<Statement> ParserImpl::parse_namespace_entry() {
  PhysicalSourceLocation loc = lex().loc_begin();
  
  if (!lex().reject(tok_eof) || !lex().reject(';'))
    return SharedPtr<Statement>();
  
  lex().expect(tok_id);
  Text identifier = lex().value().value()->text;
  
  Compiler::StatementMode mode;
  if (lex().accept(':'))
    mode = Compiler::statement_mode_value;
  else if (lex().accept(tok_op_double_colon))
    mode = Compiler::statement_mode_functional;
  else if (lex().accept(tok_op_colon_amp))
    mode = Compiler::statement_mode_ref;
  else
    lex().unexpected();
  
  SharedPtr<Expression> expr = parse_expression();
  lex().loc_end(loc);
  
  return SharedPtr<Statement>(new Statement(loc, expr, identifier, mode));
}

/**
 * \brief parse an argument list.
 * \details an argument list is a list of Expressions forming arguments to a function call.
 * \param text Text to parse.
 */
PSI_STD::vector<SharedPtr<Statement> > parse_namespace(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  PSI_STD::vector<SharedPtr<Statement> > result = ParserImpl(&lexer).parse_namespace();
  lexer.expect(tok_eof);
  return result;
}

PSI_STD::vector<SharedPtr<Expression> > ParserImpl::parse_positional_list() {
  PSI_STD::vector<SharedPtr<Expression> > result;
  if (!lex().reject(tok_eof))
    return result;
  
  do {
    result.push_back(parse_expression());
  } while (lex().accept(','));
  
  return result;
}

/**
 * \brief parse a purely positional argument list.
 * \details an argument list is a list of Expressions forming arguments to a function call.
 * \param text Text to parse.
 */
PSI_STD::vector<SharedPtr<Expression> > parse_positional_list(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  PSI_STD::vector<SharedPtr<Expression> > result = ParserImpl(&lexer).parse_positional_list();
  lexer.expect(tok_eof);
  return result;
}

SharedPtr<Expression> ParserImpl::parse_binary_expression(const char* (ParserImpl::*op_callback) (), SharedPtr<Expression> (ParserImpl::*child) ()) {
  PhysicalSourceLocation loc = lex().loc_begin();
  SharedPtr<Expression> result = (this->*child)();
  while (const char *op_func = (this->*op_callback)()) {
    PhysicalSourceLocation op_loc = lex().value().location();
    SharedPtr<Expression> rhs = (this->*child)();
    lex().loc_end(loc);
    result = binary_expr(loc, op_loc, op_func, result, rhs);
  }
  return result;
}

const char* ParserImpl::op_callback_assign() {
  return lex().accept('=') ? "__assign__" : NULL;
}

SharedPtr<Expression> ParserImpl::parse_expression() {
  return parse_binary_expression(&ParserImpl::op_callback_assign, &ParserImpl::parse_or_expression);
}

const char* ParserImpl::op_callback_or() {
  return lex().accept('|') ? "__or__" : NULL;
}

SharedPtr<Expression> ParserImpl::parse_or_expression() {
  return parse_binary_expression(&ParserImpl::op_callback_or, &ParserImpl::parse_xor_expression);
}

const char* ParserImpl::op_callback_xor() {
  return lex().accept('^') ? "__xor__" : NULL;
}

SharedPtr<Expression> ParserImpl::parse_xor_expression() {
  return parse_binary_expression(&ParserImpl::op_callback_xor, &ParserImpl::parse_and_expression);
}

const char* ParserImpl::op_callback_and() {
  return lex().accept('&') ? "__and__" : NULL;
}

SharedPtr<Expression> ParserImpl::parse_and_expression() {
  return parse_binary_expression(&ParserImpl::op_callback_and, &ParserImpl::parse_compare_expression);
}

const char* ParserImpl::op_callback_compare() {
  if (lex().accept(tok_cmp_eq)) return "__eq__";
  else if (lex().accept(tok_cmp_ne)) return "__ne__";
  else if (lex().accept(tok_cmp_ge)) return "__ge__";
  else if (lex().accept(tok_cmp_le)) return "__le__";
  else if (lex().accept('<')) return "__lt__";
  else if (lex().accept('>')) return "__gt__";
  else return NULL;
}

SharedPtr<Expression> ParserImpl::parse_compare_expression() {
  PhysicalSourceLocation loc = lex().loc_begin();

  SharedPtr<Expression> first = parse_sum_expression();
  const char *op = op_callback_compare();
  if (!op)
    return first;
  
  PSI_STD::vector<SharedPtr<Expression> > parts(1, first);
  do {
    parts.push_back(str_expression(lex().value().location(), op));
    parts.push_back(parse_sum_expression());
  } while ((op = op_callback_compare()));
  
  lex().loc_end(loc);
  
  return SharedPtr<Expression>(new EvaluateExpression(loc, str_expression(loc, "__cmp__"), parts));
}

const char* ParserImpl::op_callback_sum() {
  if (lex().accept('+')) return "__add__";
  else if (lex().accept('-')) return "__sub__";
  else return NULL;
}

SharedPtr<Expression> ParserImpl::parse_sum_expression() {
  return parse_binary_expression(&ParserImpl::op_callback_sum, &ParserImpl::parse_product_expression);
}

const char *ParserImpl::op_callback_product() {
  if (lex().accept('*')) return "__mul__";
  else if (lex().accept('/')) return "__div__";
  else if (lex().accept('%')) return "__mod__";
  else return NULL;
}

SharedPtr<Expression> ParserImpl::parse_product_expression() {
  return parse_binary_expression(&ParserImpl::op_callback_product, &ParserImpl::parse_unary_expression);
}

SharedPtr<Expression> ParserImpl::parse_unary_expression() {
  PhysicalSourceLocation loc = lex().loc_begin();
  if (lex().accept('-')) {
    PhysicalSourceLocation op_loc = lex().value().location();
    SharedPtr<Expression> param = parse_unary_expression();
    lex().loc_end(loc);
    return unary_expr(loc, op_loc, "__neg__", param);
  } else if (lex().accept('!')) {
    PhysicalSourceLocation op_loc = lex().value().location();
    SharedPtr<Expression> param = parse_unary_expression();
    lex().loc_end(loc);
    return unary_expr(loc, op_loc, "__inv__", param);
  } else {
    return parse_macro_expression();
  }
}

SharedPtr<Expression> ParserImpl::parse_macro_expression() {
  PhysicalSourceLocation loc = lex().loc_begin();
  SharedPtr<Expression> expr = parse_token_expression();
  if (!expr)
    lex().unexpected();
  
  PSI_STD::vector<SharedPtr<Expression> > first_args = parse_token_list();
  lex().loc_end(loc);
  if (!first_args.empty())
    expr.reset(new EvaluateExpression(loc, expr, first_args));
  
  while (true) {
    if (lex().accept('.')) {
      SharedPtr<Expression> member = parse_token_expression();
      if (!member)
        lex().unexpected();
      PSI_STD::vector<SharedPtr<Expression> > args = parse_token_list();
      lex().loc_end(loc);
      expr.reset(new DotExpression(loc, expr, member, args));
    } else if (lex().accept('#')) {
      PSI_STD::vector<SharedPtr<Expression> > args = parse_token_list();
      lex().loc_end(loc);
      expr.reset(new EvaluateExpression(loc, expr, args));
    } else {
      break;
    }
  }
  
  return expr;
}

SharedPtr<TokenExpression> ParserImpl::parse_token_expression() {
  switch (lex().peek().id()) {
  case tok_id:
  case tok_number:
  case tok_block_brace:
  case tok_block_bracket:
  case tok_block_square_bracket:
    lex().accept();
    return lex().value().value();
    
  default:
    return SharedPtr<TokenExpression>();
  }
}

PSI_STD::vector<SharedPtr<Expression> > ParserImpl::parse_token_list() {
  PSI_STD::vector<SharedPtr<Expression> > result;
  while (SharedPtr<TokenExpression> expr = parse_token_expression())
    result.push_back(expr);
  return result;
}

/**
 * \brief Parse a single expression.
 */
SharedPtr<Expression> parse_expression(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  SharedPtr<Expression> result = ParserImpl(&lexer).parse_expression();
  lexer.expect(tok_eof);
  return result;
}

PSI_STD::vector<TokenExpression> ParserImpl::parse_identifier_list() {
  PSI_STD::vector<TokenExpression> result;
  if (!lex().reject(tok_eof))
    return result;

  while (true) {
    lex().expect(tok_id);
    result.push_back(*lex().value().value());
    
    if (!lex().reject(tok_eof))
      return result;
    
    lex().expect(',');
  }
}

/**
 * \brief Parse a comma-separated list of tokens.
 * 
 * A trailing comma is accepted.
 */
PSI_STD::vector<TokenExpression> parse_identifier_list(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  PSI_STD::vector<TokenExpression> result = ParserImpl(&lexer).parse_identifier_list();
  lexer.expect(tok_eof);
  return result;
}

PSI_STD::vector<SharedPtr<FunctionArgument> > ParserImpl::parse_argument_list_declare() {
  PSI_STD::vector<SharedPtr<FunctionArgument> > result;
  if (!lex().reject(tok_eof))
    return result;

  result.push_back(parse_argument_declare());
  
  while (true) {
    if (lex().accept(',')) {
      // Argument declaration
      result.push_back(parse_argument_declare());
    } else if (lex().accept('@')) {
      // Interface declaration
      SharedPtr<Expression> expr = parse_expression();
      SharedPtr<FunctionArgument> arg(new FunctionArgument(expr->location, expr));
      result.push_back(arg);
    } else {
      break;
    }
  }
  
  return result;
}

SharedPtr<FunctionArgument> ParserImpl::parse_argument_declare() {
  PhysicalSourceLocation loc = lex().loc_begin();
  
  bool has_id = lex().accept(tok_id);
  
  Compiler::ParameterMode mode;
  if (lex().accept(':'))
    mode = Compiler::parameter_mode_input;
  else if (lex().accept(tok_op_double_colon))
    mode = Compiler::parameter_mode_functional;
  else if (lex().accept(tok_op_colon_amp))
    mode = Compiler::parameter_mode_io;
  else if (lex().accept(tok_op_colon_amp_amp))
    mode = Compiler::parameter_mode_rvalue;
  else if (lex().accept(tok_op_colon_right))
    mode = Compiler::parameter_mode_output;
  else {
    mode = Compiler::parameter_mode_input;
    if (has_id) {
      lex().back();
      has_id = false;
    }
  }

  Maybe<Text> identifier;
  if (has_id)
    identifier = lex().value(1).value()->text;
  
  SharedPtr<Expression> type = parse_expression();
  lex().loc_end(loc);
  
  return SharedPtr<FunctionArgument>(new FunctionArgument(loc, identifier, mode, type));
}

Maybe<Compiler::ResultMode> ParserImpl::parse_result_mode() {
  if (lex().accept(tok_op_arrow)) return Compiler::result_mode_by_value;
  else if (lex().accept(tok_op_dash_colon)) return Compiler::result_mode_functional;
  else if (lex().accept(tok_op_dash_amp)) return Compiler::result_mode_lvalue;
  else if (lex().accept(tok_op_dash_amp_amp)) return Compiler::result_mode_rvalue;
  else return Maybe<Compiler::ResultMode>();
}

FunctionArgumentDeclarations ParserImpl::parse_function_argument_declarations() {
  FunctionArgumentDeclarations args;
  
  Maybe<Compiler::ResultMode> result_mode = parse_result_mode();
  
  if (!result_mode && lex().reject(tok_eof)) {
    args.arguments = parse_argument_list_declare();
    if (lex().accept(tok_op_double_arrow)) {
      args.arguments.swap(args.implicit);
      args.arguments = parse_argument_list_declare();
    }
    result_mode = parse_result_mode();
  }
  
  if (result_mode) {
    args.return_mode = *result_mode;
    args.return_type = parse_expression();
  } else {
    args.return_mode = Compiler::result_mode_by_value;
  }
  
  return args;
}

/**
 * \brief parse a function argument declaration.
 * \details This is a list of argument declarations possibly
 * followed by a return type Expression.
 * \param text Text to parse.
 */
FunctionArgumentDeclarations parse_function_argument_declarations(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  FunctionArgumentDeclarations result = ParserImpl(&lexer).parse_function_argument_declarations();
  lexer.expect(tok_eof);
  return result;
}

/**
 * \brief parse a type argument list.
 * \details This is a list of argument declarations possibly
 * followed by a return type Expression.
 * \param text Text to parse.
 */
PSI_STD::vector<SharedPtr<FunctionArgument> > parse_type_argument_declarations(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  PSI_STD::vector<SharedPtr<FunctionArgument> > result = ParserImpl(&lexer).parse_argument_list_declare();
  lexer.expect(tok_eof);
  return result;
}

ImplementationArgumentDeclaration ParserImpl::parse_implementation_arguments() {
  ImplementationArgumentDeclaration args;
  
  if (lex().accept2(tok_id, ':')) {
    args.pattern = parse_argument_list_declare();
    lex().expect(tok_op_double_arrow);
  }
  
  args.arguments = parse_positional_list();
  
  return args;
}

ImplementationArgumentDeclaration parse_implementation_arguments(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text) {
  ParserImpl::LexerType lexer(error_context, SourceLocation(text.location, error_loc), text.begin, text.end, LexerImpl(text.data_handle));
  ImplementationArgumentDeclaration result = ParserImpl(&lexer).parse_implementation_arguments();
  lexer.expect(tok_eof);
  return result;
}

SharedPtr<Parser::TokenExpression> expression_as_token_type(const SharedPtr<Parser::Expression>& expr, Parser::TokenExpressionType type) {
  if (expr->expression_type != expression_token)
    return SharedPtr<Parser::TokenExpression>();

  SharedPtr<Parser::TokenExpression> cast_expr = checked_pointer_cast<Parser::TokenExpression>(expr);
  if (cast_expr->token_type != type)
    return SharedPtr<Parser::TokenExpression>();

  return cast_expr;
}

SharedPtr<Parser::EvaluateExpression> expression_as_evaluate(const SharedPtr<Parser::Expression>& expr) {
  if (expr->expression_type != expression_evaluate)
    return SharedPtr<Parser::EvaluateExpression>();
  
  return checked_pointer_cast<Parser::EvaluateExpression>(expr);
}
}
}
