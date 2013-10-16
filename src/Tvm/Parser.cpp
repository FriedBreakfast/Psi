#include "Parser.hpp"

#include <cstring>
#include <boost/format.hpp>

namespace Psi {
namespace Tvm {
namespace Parser {
Element::Element(PSI_MOVE_REF(Element) src)
: location(move_value(src).location) {
}

Element::Element(const PhysicalSourceLocation& location_) : location(location_) {
}

Token::Token(PSI_MOVE_REF(Token) src)
: Element(move<Element>(src)),
text(move(move_value(src).text)) {
}

Token::Token(const PhysicalSourceLocation& location_, std::string text_)
: Element(location_), text(move(text_)) {
}

Expression::Expression(PSI_MOVE_REF(Expression) src)
: Element(move<Element>(src)), expression_type(move_value(src).expression_type) {
}

Expression::Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_)
: Element(location_), expression_type(expression_type_) {
}

Expression::~Expression() {
}

NameExpression::NameExpression(const PhysicalSourceLocation& location_, Token name_)
: Expression(location_, expression_name), name(move(name_)) {
}

Expression* NameExpression::clone() const {
  return new NameExpression(*this);
}

PhiNode::PhiNode(const PhysicalSourceLocation& location_, Maybe<Token> label_, ExpressionRef expression_)
: Element(location_), label(move(label_)), expression(move(expression_)) {
}

PhiExpression::PhiExpression(const PhysicalSourceLocation& location_, ExpressionRef type_, PSI_STD::vector<PhiNode> nodes_)
: Expression(location_, expression_phi), type(move(type_)), nodes(move(nodes_)) {
}

Expression* PhiExpression::clone() const {
  return new PhiExpression(*this);
}

CallExpression::CallExpression(const PhysicalSourceLocation& location_, Token target_, PSI_STD::vector<ExpressionRef> terms_)
: Expression(location_, expression_call), target(move(target_)), terms(move(terms_)) {
}

Expression* CallExpression::clone() const {
  return new CallExpression(*this);
}

FunctionTypeExpression::FunctionTypeExpression(PSI_MOVE_REF(FunctionTypeExpression) src)
: Expression(move<Expression>(src)),
calling_convention(move_value(src).calling_convention),
sret(move_value(src).sret),
phantom_parameters(move(move_value(src).phantom_parameters)),
parameters(move(move_value(src).parameters)),
result_attributes(move(move_value(src).result_attributes)),
result_type(move(move_value(src).result_type)) {
}

FunctionTypeExpression::FunctionTypeExpression(const PhysicalSourceLocation& location_,
                                               CallingConvention calling_convention_,
                                               bool sret_,
                                               PSI_STD::vector<ParameterExpression> phantom_parameters_,
                                               PSI_STD::vector<ParameterExpression> parameters_,
                                               ParameterAttributes result_attributes_,
                                               ExpressionRef result_type_)
: Expression(location_, expression_function_type),
calling_convention(calling_convention_),
sret(sret_),
phantom_parameters(move(phantom_parameters_)),
parameters(move(parameters_)),
result_attributes(move(result_attributes_)),
result_type(move(result_type_)) {
}

Expression* FunctionTypeExpression::clone() const {
  return new FunctionTypeExpression(*this);
}

ExistsExpression::ExistsExpression(const PhysicalSourceLocation& location_,
                                   PSI_STD::vector<ParameterExpression> parameters_,
                                   ExpressionRef result_)
: Expression(location_, expression_exists),
parameters(move(parameters_)),
result(move(result_)) {
}

Expression* ExistsExpression::clone() const {
  return new ExistsExpression(*this);
}

IntegerLiteralExpression::IntegerLiteralExpression(const PhysicalSourceLocation& location_, LiteralType literal_type_, BigInteger value_)
: Expression(location_, expression_literal),
literal_type(literal_type_),
value(move(value_)) {
}

Expression* IntegerLiteralExpression::clone() const {
  return new IntegerLiteralExpression(*this);
}

NamedExpression::NamedExpression(const PhysicalSourceLocation& location_, Maybe<Token> name_, ExpressionRef expression_)
: Element(location_),
name(move(name_)),
expression(move(expression_)) {
}

ParameterExpression::ParameterExpression(const PhysicalSourceLocation& location_, Maybe<Token> name_,
                                         ParameterAttributes attributes_, ExpressionRef expression_)
: NamedExpression(location_, move(name_), move(expression_)), attributes(move(attributes_)) {
}

Block::Block(const PhysicalSourceLocation& location_, bool landing_pad_,
             Maybe<Token> name_, Maybe<Token> dominator_name_,
             PSI_STD::vector<NamedExpression> statements_)
: Element(location_),
landing_pad(landing_pad_),
name(move(name_)),
dominator_name(move(dominator_name_)),
statements(move(statements_)) {
}

GlobalElement::GlobalElement(const PhysicalSourceLocation& location_, GlobalType global_type_)
: Element(location_), global_type(global_type_) {
}

GlobalElement::~GlobalElement() {
}

Function::Function(const PhysicalSourceLocation& location_,
                   Linkage linkage_,
                   FunctionTypeExpression type_)
: GlobalElement(location_, global_function),
linkage(linkage_),
type(move(type_)) {
}

Function::Function(const PhysicalSourceLocation& location_,
                   Linkage linkage_,
                   FunctionTypeExpression type_,
                   PSI_STD::vector<Block> blocks_)
: GlobalElement(location_, global_function),
linkage(linkage_),
type(move(type_)),
blocks(move(blocks_)) {
}

GlobalElement* Function::clone() const {
  return new Function(*this);
}

GlobalVariable::GlobalVariable(const PhysicalSourceLocation& location_,
                               bool constant_,
                               Linkage linkage_,
                               ExpressionRef type_)
: GlobalElement(location_, global_variable),
constant(constant_),
linkage(linkage_),
type(move(type_)) {
}

GlobalVariable::GlobalVariable(const PhysicalSourceLocation& location_,
                               bool constant_,
                               Linkage linkage_,
                               ExpressionRef type_,
                               ExpressionRef value_)
: GlobalElement(location_, global_variable),
constant(constant_),
linkage(linkage_),
type(move(type_)),
value(move(value_)) {
}

GlobalElement* GlobalVariable::clone() const {
  return new GlobalVariable(*this);
}

GlobalDefine::GlobalDefine(const PhysicalSourceLocation& location_,
                           ExpressionRef value_)
: GlobalElement(location_, global_define),
value(move(value_)) {
}

GlobalElement* GlobalDefine::clone() const {
  return new GlobalDefine(*this);
}

RecursiveType::RecursiveType(const PhysicalSourceLocation& location_,
                             PSI_STD::vector<ParameterExpression> phantom_parameters_,
                             PSI_STD::vector<ParameterExpression> parameters_,
                             ExpressionRef result_)
: GlobalElement(location_, global_recursive),
phantom_parameters(move(phantom_parameters_)),
parameters(move(parameters_)),
result(move(result_)) {
}

GlobalElement* RecursiveType::clone() const {
  return new RecursiveType(*this);
}

NamedGlobalElement::NamedGlobalElement(const PhysicalSourceLocation& location_, Token name_, GlobalElementRef value_)
: Element(location_),
name(move(name_)),
value(move(value_)) {
}

// Tokens which are not ASCII characters
enum LongToken {
  tok_eof=256,
  
  tok_id,
  tok_op,
  tok_number,
  tok_function,
  tok_recursive,
  tok_global,
  tok_define,
  tok_phi,
  tok_exists,
  tok_block,
  tok_landing_pad,
  tok_extern,
  tok_const,

  // Function attributes
  tok_cc_c,
  tok_sret,
  tok_llvm_byval,
  tok_llvm_inreg,
  
  // Linkage types
  tok_local,
  tok_private,
  tok_odr,
  tok_export,
  tok_import
};

class LexerImpl {
public:
  struct KeywordTokenPair {
    const char *keyword;
    int token;
  };
  
  static const std::size_t n_keywords = 18;
  static const KeywordTokenPair keywords[n_keywords];

  class LexerValue {
    int m_id;
    PhysicalSourceLocation m_location;
    ExpressionRef m_expression;
    Maybe<Token> m_token;
    
  public:
    LexerValue();
    LexerValue(PSI_MOVE_REF(LexerValue) src);
    LexerValue(int id, PhysicalSourceLocation location);
    LexerValue(int id, PhysicalSourceLocation location, ExpressionRef src);
    LexerValue(int id, PhysicalSourceLocation location, Token src);
    
    int id() {return m_id;}
    const PhysicalSourceLocation& location() {return m_location;}
    ExpressionRef& expression() {return m_expression;}
    Token& token() {return *m_token;}
  };

  LexerImpl(CompileErrorContext& error_context, const SourceLocation& loc, const char *start, const char *end);

  CompileErrorPair error_loc(const PhysicalSourceLocation& loc);
  PSI_ATTRIBUTE((PSI_NORETURN)) void error(const PhysicalSourceLocation& loc, const std::string& message);
  template<typename T> PSI_ATTRIBUTE((PSI_NORETURN)) void error(const PhysicalSourceLocation& loc, const T& message) {error(loc, CompileError::to_str(message));}

  LexerValue& peek();
  bool reject(int t);
  void accept();
  bool accept(int t);
  bool accept2(int a, int b);
  void expect(int t);
  void back();
  PSI_ATTRIBUTE((PSI_NORETURN)) void unexpected();
  
  LexerValue& value(unsigned n=0);
  const PhysicalSourceLocation& loc_begin() {return peek().location();}
  void loc_end(PhysicalSourceLocation& loc);

private:
  CompileErrorContext *m_error_context;
  LogicalSourceLocationPtr m_error_location;

  PhysicalSourceLocation m_location;
  const char *m_current, *m_end;

  static const unsigned n_backtrack = 3;
  LexerValue m_values[n_backtrack];
  unsigned m_values_pos, m_values_begin, m_values_end;

  std::string error_name(int tok);
  std::string error_name(LexerValue& value);
  int keyword_to_token(const char *begin, const char *end);
  LiteralType signed_literal_type(const PhysicalSourceLocation& loc, char c);
  LiteralType unsigned_literal_type(const PhysicalSourceLocation& loc, char c);
  LexerValue lex();
  void lex_accept();
  const char* lex_accept_token_chars();
  BigInteger lex_integer(const PhysicalSourceLocation& loc, LiteralType type, const char *start, const char *end);
};

// Must be maintained in lexicographical order
const LexerImpl::KeywordTokenPair LexerImpl::keywords[LexerImpl::n_keywords] = {
  {"block", tok_block},
  {"cc_c", tok_cc_c},
  {"const", tok_const},
  {"define", tok_define},
  {"exists", tok_exists},
  {"export", tok_export},
  {"extern", tok_extern},
  {"function", tok_function},
  {"global", tok_global},
  {"import", tok_import},
  {"llvm_byval", tok_llvm_byval},
  {"llvm_inreg", tok_llvm_inreg},
  {"local", tok_local},
  {"odr", tok_odr},
  {"phi", tok_phi},
  {"private", tok_private},
  {"recursive", tok_recursive},
  {"sret", tok_sret},
};

LexerImpl::LexerValue::LexerValue() {
}

LexerImpl::LexerValue::LexerValue(PSI_MOVE_REF(LexerValue) src)
: m_expression(move(move_value(src).m_expression)),
m_token(move(move_value(src).m_token)) {
}

LexerImpl::LexerValue::LexerValue(int id, PhysicalSourceLocation loc)
: m_id(id), m_location(loc) {
}

LexerImpl::LexerValue::LexerValue(int id, PhysicalSourceLocation loc, ExpressionRef src)
: m_id(id), m_location(loc), m_expression(move(src)) {

}

LexerImpl::LexerValue::LexerValue(int id, PhysicalSourceLocation loc, Token src)
: m_id(id), m_location(loc), m_token(move(src)) {
}

LexerImpl::LexerImpl(CompileErrorContext& error_context, const SourceLocation& loc, const char* start, const char* end)
: m_error_context(&error_context), m_error_location(loc.logical),
m_location(loc.physical), m_current(start), m_end(end) {
  m_location.last_column = m_location.first_column;
  m_location.last_line = m_location.first_line;
  
  // Grab first token
  m_values[0] = lex();
  m_values_begin = 0;
  m_values_pos = 0;
  m_values_end = 1;
}

CompileErrorPair LexerImpl::error_loc(const PhysicalSourceLocation& loc) {
  return CompileErrorPair(*m_error_context, SourceLocation(loc, m_error_location));
}

void LexerImpl::error(const PhysicalSourceLocation& loc, const std::string& message) {
  error_loc(loc).error_throw(message);
}

/**
 * \brief Lexer value \c n items back
 * 
 * This does not currently do proper error checking to see whether \c n is out
 * of bounds as defined by \c m_values_begin and \c m_values_end.
 */
LexerImpl::LexerValue& LexerImpl::value(unsigned n) {
  ++n;
  PSI_ASSERT(n < n_backtrack);
  
  unsigned idx = m_values_pos;
  if (idx >= n)
    idx -= n;
  else
    idx += n_backtrack - n;
  return m_values[idx];
}

/// \brief Peek at the next token
LexerImpl::LexerValue& LexerImpl::peek() {
  return m_values[m_values_pos];
}

/// \brief Accept the next token unconditionally
void LexerImpl::accept() {
  ++m_values_pos;
  if (m_values_pos == n_backtrack)
    m_values_pos = 0;
  
  if (m_values_pos == m_values_end) {
    m_values[m_values_pos] = lex();
    
    if (m_values_pos == m_values_begin) {
      ++m_values_begin;
      if (m_values_begin == n_backtrack)
        m_values_begin = 0;
    }
    
    ++m_values_end;
    if (m_values_end == n_backtrack)
      m_values_end = 0;
  }
}

void LexerImpl::back() {
  PSI_ASSERT(m_values_pos != m_values_begin);
  if (m_values_pos == 0)
    m_values_pos = n_backtrack - 1;
  else
    --m_values_pos;
}

/// \brief Return true if the next token is not \c t
bool LexerImpl::reject(int t) {
  return peek().id() != t;
}

/// \brief Accept the next token if it is a \c t
bool LexerImpl::accept(int t) {
  if (peek().id() == t) {
    accept();
    return true;
  }
  
  return false;
}

bool LexerImpl::accept2(int a, int b) {
  if (accept(a)) {
    if (accept(b))
      return true;
    else
      back();
  }
  
  return false;
}

std::string LexerImpl::error_name(int tok) {
  if (tok <= 256) {
    return boost::str(boost::format("'%c'") % char(tok));
  } else {
    switch (tok) {
    case tok_id: return "identifier";
    case tok_op: return "operator";
    case tok_eof: return "end-of-file";
    case tok_number: return "number";
      
    default: {
      for (std::size_t ii = 0, ie = n_keywords; ii != ie; ++ii) {
        if (tok == keywords[ii].token)
          return keywords[ii].keyword;
      }
      
      return boost::str(boost::format("%d") % tok);
    }
    }
  }
}

std::string LexerImpl::error_name(LexerValue& value) {
  switch (value.id()) {
  case tok_id: return boost::str(boost::format("identifier '%%%s'") % value.token().text);
  case tok_op: return boost::str(boost::format("operator '%s'") % value.token().text);
  default: return error_name(value.id());
  }
}

/// \brief Require the next token to be a \c t
void LexerImpl::expect(int t) {
  if (peek().id() != t)
    error(peek().location(), boost::format("Unexpected token %s, expected %s") % error_name(peek()) % error_name(t));
  accept();
}

void LexerImpl::unexpected() {
  error(peek().location(), boost::format("Unexpected token %s") % error_name(peek()));
}

namespace {
  struct KeywordStrCompareData {
    const char *start, *end;
    
    KeywordStrCompareData(const char *start_, const char *end_) : start(start_), end(end_) {}
    KeywordStrCompareData(LexerImpl::KeywordTokenPair tp) : start(tp.keyword), end(NULL) {}
  };
  
  bool operator < (KeywordStrCompareData lhs, KeywordStrCompareData rhs) {
    const char *l = lhs.start, *r = rhs.start;
    const char *le = lhs.end, *re = rhs.end;
    while (true) {
      char lc = (l != le) ? *l : '\0';
      char rc = (r != re) ? *r : '\0';
      
      if (lc < rc)
        return true;
      else if (lc > rc)
        return false;
      else if (lc == '\0')
        return false;
      
      ++l; ++r;
    }
  }
}

int LexerImpl::keyword_to_token(const char *start, const char *end) {
  KeywordStrCompareData kw_test(start, end);
  const KeywordTokenPair *kw_end = keywords + n_keywords;
  const KeywordTokenPair *kw_found = std::lower_bound(keywords, kw_end, kw_test);
  if (kw_found != kw_end) {
    if (!(kw_test < *kw_found))
      return kw_found->token;
  }
  
  return -1;
}

/// Map char to signed literal type ID
LiteralType LexerImpl::signed_literal_type(const PhysicalSourceLocation& loc, char c) {
  switch (c) {
  case 'b': return literal_byte;
  case 's': return literal_short;
  case 'i': return literal_int;
  case 'l': return literal_long;
  case 'q': return literal_quad;
  case 'p': return literal_intptr;
  default: error(loc, boost::format("Unknown literal type '%c'") % c);
  }
}

/// Map char to unsigned literal type ID
LiteralType LexerImpl::unsigned_literal_type(const PhysicalSourceLocation& loc, char c) {
  switch (c) {
  case 'b': return literal_ubyte;
  case 's': return literal_ushort;
  case 'i': return literal_uint;
  case 'l': return literal_ulong;
  case 'q': return literal_uquad;
  case 'p': return literal_uintptr;
  default: error(loc, boost::format("Unknown literal type '%c'") % c);
  }
}

/// Accept one character
void LexerImpl::lex_accept() {
  ++m_current;
  ++m_location.last_column;
}

/// Grab all token characters
const char* LexerImpl::lex_accept_token_chars() {
  const char *start = m_current;

  // Grab all token characters
  while ((m_current != m_end) && token_char(*m_current))
    lex_accept();
  
  if (start == m_current)
    error(m_location, "Zero length token found");

  return start;
}

BigInteger LexerImpl::lex_integer(const PhysicalSourceLocation& loc, LiteralType type, const char *start, const char *end) {
  if (start == end)
    error(loc, "Number literal is too short");
  
  unsigned bits;
  switch (type) {
  case literal_byte: case literal_ubyte: bits = 8; break;
  case literal_short: case literal_ushort: bits = 16; break;
  case literal_int: case literal_uint: bits = 32; break;
  case literal_long: case literal_ulong: bits = 64; break;
  case literal_quad: case literal_uquad: bits = 128; break;
  case literal_intptr: case literal_uintptr: bits = 64; break;
  default: PSI_FAIL("Unknown integer literal type");
  }
  
  BigInteger value(bits);
  
  unsigned base;
  switch (*start) {
  case 'x':
    ++start;
    base = 16;
    break;
  
  default:
    base = 10;
    break;
  }
  
  if (start == end)
    error(loc, "Number literal is too short");

  bool negative;
  if (*start == '-') {
    negative = true;
    ++start;
  }

  if (start == end)
    error(loc, "Number literal is too short");

  value.parse(error_loc(loc), start, end, negative, base);
  
  return value;
}

/// Token parser
LexerImpl::LexerValue LexerImpl::lex() {
  // Skip whitespace
  while(m_current != m_end) {
    if (std::strchr(" \t\r\v", *m_current)) {
      lex_accept();
    } else if (*m_current == '\n') {
      ++m_location.last_line;
      m_location.last_column = 1;
      lex_accept();
    } else {
      break;
    }
  }

  if (m_current == m_end) {
    // End-of-stream
    return LexerValue(tok_eof, m_location);
  }

  m_location.first_line = m_location.last_line;
  m_location.first_column = m_location.last_column;

  // Get token type
  switch(*m_current) {
  case '#': {
    lex_accept();
    const char *start = lex_accept_token_chars();
    
    if (m_current - start < 2)
      error(m_location, "Number literal is too short");

    LiteralType number_type;
    Psi::PhysicalSourceLocation literal_loc(m_location);
    if (start[0] == 'u') {
      number_type = unsigned_literal_type(m_location, start[1]);
      literal_loc.first_column += 2;
      start += 2;
    } else {
      number_type = signed_literal_type(m_location, start[0]);
      literal_loc.first_column += 1;
      start += 1;
    }
    
    ExpressionRef expr(new IntegerLiteralExpression(literal_loc, number_type, lex_integer(m_location, number_type, start, m_current)));
    return LexerValue(tok_number, literal_loc, move(expr));
  }

  case '%': {
    lex_accept();
    const char *start = lex_accept_token_chars();

    std::string text;
    for (const char *p = start; p != m_current; ++p) {
      if (*p != '%') {
        text.push_back(*p);
      } else {
        int c = 0;
        ++p;
        if (p != m_current) {
          c = *p - '0';
          ++p;
          if (p != m_current) {
            c <<= 8;
            c |= *p - '0';
          }
        }
        text.push_back(c);
      }
    }
    
    return LexerValue(tok_id, m_location, Token(m_location, move(text)));
  }

  default: {
    PSI_ASSERT(*m_current != '%');
    if (token_char(*m_current)) {
      const char *start = lex_accept_token_chars();
      int kw = keyword_to_token(start, m_current);
      if (kw >= 0) {
        // A keyword
        return LexerValue(kw, m_location);
      } else {
        // Not really a keyword, but an operator
        return LexerValue(tok_op, m_location, Token(m_location, std::string(start, m_current)));
      }
    } else {
      int tok = *m_current;
      lex_accept();
      return LexerValue(tok, m_location);
    }
  }
  }
}

void LexerImpl::loc_end(PhysicalSourceLocation& loc) {
  loc.last_line = m_location.last_line;
  loc.last_column = m_location.last_column;
}

class ParserImpl {
public:
  ParserImpl(LexerImpl *lexer) : m_lexer(lexer) {}
  
  LexerImpl& lex() {return *m_lexer;}

  PSI_STD::vector<NamedGlobalElement> parse_globals();
  GlobalElementRef parse_global_element();
  ExpressionRef parse_root_expression();
  ExpressionRef parse_expression();
  Tvm::Linkage parse_linkage();
  FunctionTypeExpression parse_function_type();
  PSI_STD::vector<ParameterExpression> parse_parameter_list();
  ParameterExpression parse_parameter();
  ParameterAttributes parse_attribute_list();
  PSI_STD::vector<Block> parse_function_body();
  PSI_STD::vector<NamedExpression> parse_statement_list();
  PSI_STD::vector<PhiNode> parse_phi_nodes();
  
private:
  LexerImpl *m_lexer;
};

PSI_STD::vector<NamedGlobalElement> ParserImpl::parse_globals() {
  PSI_STD::vector<NamedGlobalElement> result;
  while (true) {
    if (lex().accept(tok_eof))
      break;
    
    PhysicalSourceLocation loc = lex().loc_begin();
    lex().expect(tok_id);
    Token name = lex().value().token();
    lex().expect('=');
    GlobalElementRef global = parse_global_element();
    lex().expect(';');
    lex().loc_end(loc);
    result.push_back(NamedGlobalElement(loc, move(name), move(global)));
  }
  return result;
}

Linkage ParserImpl::parse_linkage() {
  if (lex().accept(tok_private)) {
    return link_private;
  } else if (lex().accept(tok_odr)) {
    return link_one_definition;
  } else if (lex().accept(tok_export)) {
    return link_export;
  } else if (lex().accept(tok_import)) {
    return link_import;
  } else {
    return link_private;
  }
}

GlobalElementRef ParserImpl::parse_global_element() {
  PhysicalSourceLocation loc = lex().loc_begin();
  if (lex().accept(tok_global)) {
    // Global variable
    bool is_const = lex().accept(tok_const);
    Linkage linkage = parse_linkage();
    ExpressionRef type, value;
    type = parse_expression();
    if (lex().reject(';'))
      value = parse_expression();
    lex().loc_end(loc);
    return GlobalElementRef(new GlobalVariable(loc, is_const, linkage, type, value));
  } else if (lex().accept(tok_define)) {
    // Constant def
    ExpressionRef value = parse_root_expression();
    lex().loc_end(loc);
    return GlobalElementRef(new GlobalDefine(loc, move(value)));
  } else if (lex().accept(tok_recursive)) {
    // Recursive type
    PSI_STD::vector<ParameterExpression> parameters, phantom_parameters;
    lex().expect('(');
    parameters = parse_parameter_list();
    if (lex().accept('|')) {
      parameters.swap(phantom_parameters);
      parameters = parse_parameter_list();
    }
    lex().expect(')');
    lex().expect('>');
    ExpressionRef result = parse_expression();
    lex().loc_end(loc);
    return GlobalElementRef(new RecursiveType(loc, move(phantom_parameters), move(parameters), move(result)));
  } else {
    // Function
    Linkage linkage = parse_linkage();
    FunctionTypeExpression type = parse_function_type();
    PSI_STD::vector<Block> blocks;
    if (lex().accept('{')) {
      blocks = parse_function_body();
      lex().expect('}');
    }
    lex().loc_end(loc);
    return GlobalElementRef(new Function(loc, linkage, move(type), move(blocks)));
  }
}

PSI_STD::vector<ParameterExpression> ParserImpl::parse_parameter_list() {
  PSI_STD::vector<ParameterExpression> result;
  if (!lex().reject(')') || !lex().reject('|'))
    return result;
  do {
    result.push_back(parse_parameter());
  } while (lex().accept(','));
  return result;
}

ParameterExpression ParserImpl::parse_parameter() {
  PhysicalSourceLocation loc = lex().loc_begin();
  Maybe<Token> id;
  ParameterAttributes attrs;
  if (lex().accept2(tok_id, ':')) {
    id = lex().value(1).token();
    attrs = parse_attribute_list();
  } else if (lex().accept(':')) {
    attrs = parse_attribute_list();
  }
  
  ExpressionRef type = parse_root_expression();
  lex().loc_end(loc);
  return ParameterExpression(loc, move(id), move(attrs), move(type));
}

ParameterAttributes ParserImpl::parse_attribute_list() {
  ParameterAttributes attrs;
  while (true) {
    if (lex().accept(tok_llvm_byval))
      attrs.flags |= ParameterAttributes::llvm_byval;
    else if (lex().accept(tok_llvm_inreg))
      attrs.flags |= ParameterAttributes::llvm_inreg;
    else
      break;
  }
  return attrs;
}

PSI_STD::vector<Block> ParserImpl::parse_function_body() {
  PSI_STD::vector<Block> blocks;
  
  Maybe<Token> name, dominator_name;
  PhysicalSourceLocation loc = lex().loc_begin();
  bool landing_pad = false;
  
  while (true) {
    PSI_STD::vector<NamedExpression> statements = parse_statement_list();
    lex().loc_end(loc);
    
    blocks.push_back(Block(loc, landing_pad, move(name), move(dominator_name), move(statements)));
    
    if (!lex().reject('}'))
      break;
    
    loc = lex().loc_begin();
    
    if (lex().accept(tok_landing_pad))
      landing_pad = true;
    else if (lex().accept(tok_block))
      landing_pad = false;
    else
      lex().unexpected();
    
    lex().expect(tok_id);
    name = lex().value().token();
    if (lex().accept('(')) {
      lex().expect(tok_id);
      dominator_name = lex().value().token();
      lex().expect(')');
    }
    lex().expect(':');
  }
  
  return blocks;
}

PSI_STD::vector<NamedExpression> ParserImpl::parse_statement_list() {
  PSI_STD::vector<NamedExpression> result;
  
  while (true) {
    if (!lex().reject('}') || !lex().reject(tok_block) || !lex().reject(tok_landing_pad))
      return result;
    
    PhysicalSourceLocation loc = lex().loc_begin();
    
    Maybe<Token> name;
    if (lex().accept2(tok_id, '='))
      name = lex().value(1).token();
    
    ExpressionRef expr;
    if (lex().accept(tok_phi)) {
      ExpressionRef type = parse_expression();
      lex().expect(':');
      lex().loc_end(loc);
      PSI_STD::vector<PhiNode> entries = parse_phi_nodes();
      expr.reset(new PhiExpression(loc, move(type), move(entries)));
    } else {
      expr = parse_root_expression();
    }
    
    lex().expect(';');
    lex().loc_end(loc);
    
    result.push_back(NamedExpression(loc, move(name), move(expr)));
  }
}

PSI_STD::vector<PhiNode> ParserImpl::parse_phi_nodes() {
  PSI_STD::vector<PhiNode> result;
  do {
    PhysicalSourceLocation loc = lex().loc_begin();
    
    Maybe<Token> name;
    if (lex().accept(tok_id))
      name = lex().value().token();
    
    lex().expect('>');
    
    ExpressionRef value = parse_expression();
    lex().loc_end(loc);
    
    result.push_back(PhiNode(loc, move(name), move(value)));
  } while (lex().accept(','));
  
  return result;
}

ExpressionRef ParserImpl::parse_root_expression() {
  if (lex().accept(tok_op)) {
    PhysicalSourceLocation loc = lex().value().location();
    Token name = move(lex().value().token());
    PSI_STD::vector<ExpressionRef> terms;
    while (true) {
      if (!lex().reject(';') || !lex().reject(',') || !lex().reject(')') || !lex().reject('|'))
        break;
      terms.push_back(parse_expression());
    }
    lex().loc_end(loc);
    return ExpressionRef(new CallExpression(loc, move(name), move(terms)));
  } else if (lex().accept(tok_exists)) {
    PhysicalSourceLocation loc = lex().value().location();
    lex().expect('(');
    PSI_STD::vector<ParameterExpression> parameters = parse_parameter_list();
    lex().expect(')');
    lex().expect('>');
    ExpressionRef type = parse_expression();
    lex().loc_end(loc);
    return ExpressionRef(new ExistsExpression(loc, parameters, type));
  } else if (!lex().reject(tok_function)) {
    return ExpressionRef(new FunctionTypeExpression(parse_function_type()));
  } else {
    return parse_expression();
  }
}

ExpressionRef ParserImpl::parse_expression() {
  if (lex().accept('(')) {
    ExpressionRef expr = parse_root_expression();
    lex().expect(')');
    return expr;
  } else if (lex().accept(tok_number)) {
    return lex().value().expression();
  } else if (lex().accept(tok_id)) {
    return ExpressionRef(new NameExpression(lex().value().location(), lex().value().token()));
  } else if (lex().accept(tok_op)) {
    return ExpressionRef(new CallExpression(lex().value().location(), lex().value().token(), default_));
  } else {
    lex().unexpected();
  }
}

FunctionTypeExpression ParserImpl::parse_function_type() {
  PhysicalSourceLocation loc = lex().loc_begin();
  
  lex().expect(tok_function);
  
  CallingConvention cc = cconv_c;
  if (lex().accept(tok_cc_c))
    cc = cconv_c;
  
  bool sret = lex().accept(tok_sret);
  lex().expect('(');
  PSI_STD::vector<ParameterExpression> phantom_parameters, parameters;
  parameters = parse_parameter_list();
  if (lex().accept('|')) {
    phantom_parameters.swap(parameters);
    parameters = parse_parameter_list();
  }
  lex().expect(')');
  lex().expect('>');
  
  ParameterAttributes ret_attrs = parse_attribute_list();
  ExpressionRef ret_type = parse_expression();
  
  lex().loc_end(loc);
  
  return FunctionTypeExpression(loc, cc, sret, move(phantom_parameters), move(parameters), move(ret_attrs), move(ret_type));
}
}

PSI_STD::vector<Parser::NamedGlobalElement> parse(CompileErrorContext& error_context, const SourceLocation& loc, const char *begin, const char *end) {
  Parser::LexerImpl lexer(error_context, loc, begin, end);
  Parser::ParserImpl parser(&lexer);
  return parser.parse_globals();
}

PSI_STD::vector<Parser::NamedGlobalElement> parse(CompileErrorContext& error_context, const SourceLocation& loc, const char *begin) {
  return parse(error_context, loc, begin, begin+std::strlen(begin));
}
}
}
