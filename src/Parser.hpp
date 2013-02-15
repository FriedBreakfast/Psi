#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <stdexcept>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "SourceLocation.hpp"
#include "Runtime.hpp"
#include "Enums.hpp"

namespace Psi {
  namespace Parser {
    using Compiler::ResultMode;
    using Compiler::ParameterMode;
    
    struct ParserLocation {
      PhysicalSourceLocation location;
      const char *begin, *end;
      
      String to_string() const {return String(begin, end);}
    };

    struct Element {
      Element(const ParserLocation& location_);

      ParserLocation location;
    };

    struct Expression : Element {
      Expression(const ParserLocation& location_, ExpressionType expression_type_);
      virtual ~Expression();

      ExpressionType expression_type;
    };

    struct TokenExpression : Expression {
      TokenExpression(const ParserLocation& location_, TokenExpressionType token_type_, const ParserLocation& text_);
      virtual ~TokenExpression();

      TokenExpressionType token_type;
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
    
    struct Statement : Element {
      Statement(const ParserLocation& source_, const SharedPtr<Expression>& expression_, const boost::optional<ParserLocation>& name_, int mode_);
      ~Statement();

      boost::optional<ParserLocation> name;
      int mode;
      SharedPtr<Expression> expression;
    };
    
    struct Implementation : Element {
      Implementation(const ParserLocation& source_, bool constructor_, const SharedPtr<Expression>& interface_, const SharedPtr<Expression>& arguments_, const SharedPtr<Expression>& value_);
      ~Implementation();
      
      bool constructor;
      SharedPtr<Expression> interface;
      SharedPtr<Expression> arguments;
      SharedPtr<Expression> value;
    };
    
    struct Lifecycle : Element {
      Lifecycle(const ParserLocation& source_, const ParserLocation& function_name_, const ParserLocation& dest_name_,
                const boost::optional<ParserLocation>& src_name_, const SharedPtr<TokenExpression>& body_);
      ~Lifecycle();
      
      ParserLocation function_name;
      ParserLocation dest_name;
      boost::optional<ParserLocation> src_name;
      SharedPtr<TokenExpression> body;
    };
    
    struct FunctionArgument : Element {
      FunctionArgument(const ParserLocation& source_, const boost::optional<ParserLocation>& name_,
                       int mode_, const SharedPtr<Expression>& type_);

      boost::optional<ParserLocation> name;
      int mode;
      SharedPtr<Expression> type;
    };

    class ParseError : public std::runtime_error {
      PhysicalSourceLocation m_location;
      std::string m_reason;
      
    public:
      ParseError(const PhysicalSourceLocation& location, const std::string& reason);
      virtual ~ParseError() throw();
      
      const PhysicalSourceLocation& location() const {return m_location;}
      const std::string& reason() const {return m_reason;}
    };

    PSI_STD::vector<SharedPtr<Statement> > parse_statement_list(const ParserLocation&);
    PSI_STD::vector<SharedPtr<Implementation> > parse_implementation_list(const ParserLocation&);
    PSI_STD::vector<SharedPtr<Lifecycle> > parse_lifecycle_list(const ParserLocation&);
    PSI_STD::vector<SharedPtr<Statement> > parse_namespace(const ParserLocation&);
    PSI_STD::vector<SharedPtr<Expression> > parse_positional_list(const ParserLocation&);
    SharedPtr<Expression> parse_expression(const ParserLocation& text);
    PSI_STD::vector<TokenExpression> parse_identifier_list(const ParserLocation&);

    struct ImplicitArgumentDeclarations {
      PSI_STD::vector<SharedPtr<FunctionArgument> > arguments;
      PSI_STD::vector<SharedPtr<Expression> > interfaces;
    };
    
    struct ArgumentDeclarations {
      PSI_STD::vector<SharedPtr<FunctionArgument> > arguments;
      SharedPtr<FunctionArgument> return_type;
    };

    ArgumentDeclarations parse_function_argument_declarations(const ParserLocation&);
    ImplicitArgumentDeclarations parse_function_argument_implicit_declarations(const ParserLocation& text);
    SharedPtr<TokenExpression> expression_as_token_type(const SharedPtr<Expression>& expr, TokenExpressionType type);
  }
}

#endif
