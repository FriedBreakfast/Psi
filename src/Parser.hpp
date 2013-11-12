#ifndef HPP_PSI_PARSER
#define HPP_PSI_PARSER

#include <stdexcept>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include "ErrorContext.hpp"
#include "SourceLocation.hpp"
#include "Runtime.hpp"
#include "Enums.hpp"

namespace Psi {
  namespace Parser {
    struct Text {
      Text();
      PSI_COMPILER_EXPORT Text(const PhysicalSourceLocation& location, const Psi::SharedPtrHandle& data_handle, const char *begin, const char *end);
      String str() const;

      PhysicalSourceLocation location;
      SharedPtrHandle data_handle;
      const char *begin, *end;
    };

    struct Element {
      Element(const PhysicalSourceLocation& location_);

      PhysicalSourceLocation location;
    };
    
    struct Expression : Element {
      Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_);
      virtual ~Expression();

      ExpressionType expression_type;
    };

    struct TokenExpression : Expression {
      TokenExpression(const PhysicalSourceLocation& location_, TokenExpressionType token_type_, const Text& text);
      virtual ~TokenExpression();

      TokenExpressionType token_type;
      Text text;
    };

    struct EvaluateExpression : Expression {
      EvaluateExpression(const PhysicalSourceLocation& location_, const SharedPtr<Expression>& object_, const PSI_STD::vector<SharedPtr<Expression> >& parameters_);
      virtual ~EvaluateExpression();

      SharedPtr<Expression> object;
      PSI_STD::vector<SharedPtr<Expression> > parameters;
    };
    
    struct DotExpression : Expression {
      DotExpression(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& obj_, const SharedPtr<Expression>& member_, const PSI_STD::vector<SharedPtr<Expression> >& parameters_);
      virtual ~DotExpression();
      
      SharedPtr<Expression> object;
      SharedPtr<Expression> member;
      PSI_STD::vector<SharedPtr<Expression> > parameters;
    };
    
    struct Statement : Element {
      Statement(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& expression_, const Maybe<Text>& name_, int mode_);
      ~Statement();

      Maybe<Text> name;
      int mode;
      SharedPtr<Expression> expression;
    };
    
    struct Implementation : Element {
      Implementation(const PhysicalSourceLocation& source_, bool constructor_, const SharedPtr<Expression>& interface_, const SharedPtr<Expression>& arguments_, const SharedPtr<Expression>& value_);
      ~Implementation();
      
      bool constructor;
      SharedPtr<Expression> interface;
      SharedPtr<Expression> arguments;
      SharedPtr<Expression> value;
    };
    
    struct Lifecycle : Element {
      Lifecycle(const PhysicalSourceLocation& source_, const Text& function_name_, const Text& dest_name_,
                const Maybe<Text>& src_name_, const SharedPtr<TokenExpression>& body_);
      ~Lifecycle();
      
      Text function_name;
      Text dest_name;
      Maybe<Text> src_name;
      SharedPtr<TokenExpression> body;
    };
    
    struct FunctionArgument : Element {
      FunctionArgument(const PhysicalSourceLocation& source_, const Maybe<Text>& name_,
                       Compiler::ParameterMode mode_, const SharedPtr<Expression>& type_);
      FunctionArgument(const PhysicalSourceLocation& source_, const SharedPtr<Expression>& interface_);

      bool is_interface;
      Maybe<Text> name;
      Compiler::ParameterMode mode;
      SharedPtr<Expression> type;
    };

    PSI_COMPILER_EXPORT PSI_STD::vector<SharedPtr<Statement> > parse_statement_list(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text&);
    PSI_COMPILER_EXPORT PSI_STD::vector<SharedPtr<Statement> > parse_namespace(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text&);
    PSI_COMPILER_EXPORT SharedPtr<Expression> parse_expression(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text);

    PSI_STD::vector<SharedPtr<Expression> > parse_positional_list(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text&);
    PSI_STD::vector<TokenExpression> parse_identifier_list(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text&);
    
    struct FunctionArgumentDeclarations {
      PSI_STD::vector<SharedPtr<FunctionArgument> > implicit;
      PSI_STD::vector<SharedPtr<FunctionArgument> > arguments;
      Compiler::ResultMode return_mode;
      SharedPtr<Expression> return_type;
    };

    PSI_STD::vector<SharedPtr<FunctionArgument> > parse_type_argument_declarations(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text);
    FunctionArgumentDeclarations parse_function_argument_declarations(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text);
    
    struct ImplementationArgumentDeclaration {
      PSI_STD::vector<SharedPtr<FunctionArgument> > pattern;
      PSI_STD::vector<SharedPtr<Expression> > arguments;
    };
    
    ImplementationArgumentDeclaration parse_implementation_arguments(CompileErrorContext& error_context, const LogicalSourceLocationPtr& error_loc, const Text& text);
    
    bool expression_is_str(const SharedPtr<Expression>& expr, const char *str);
    SharedPtr<TokenExpression> expression_as_token_type(const SharedPtr<Expression>& expr, TokenExpressionType type);
    SharedPtr<Parser::EvaluateExpression> expression_as_evaluate(const SharedPtr<Parser::Expression>& expr);
  }
}

#endif
