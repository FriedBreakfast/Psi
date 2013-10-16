#ifndef HPP_PSI_TVM_PARSER
#define HPP_PSI_TVM_PARSER

#include <string>

#include "Core.hpp"
#include "BigInteger.hpp"
#include "../SourceLocation.hpp"

namespace Psi {
  namespace Tvm {
    namespace Parser {
      struct Element {
        Element(PSI_MOVE_REF(Element) src);
        Element(const PhysicalSourceLocation& location_);

        PhysicalSourceLocation location;
      };

      struct Token : Element {
        Token(PSI_MOVE_REF(Token) src);
        Token(const PhysicalSourceLocation& location_, std::string text_);

        std::string text;
      };

      struct Expression;
      typedef ClonePtr<Expression> ExpressionRef;

      struct NamedExpression : Element {
        NamedExpression(const PhysicalSourceLocation& location_, Maybe<Token> name_, ExpressionRef expression_);

        Maybe<Token> name;
        ExpressionRef expression;
      };
      
      struct ParameterExpression : NamedExpression {
        ParameterExpression(const PhysicalSourceLocation& location_, Maybe<Token> name_, ParameterAttributes attributes_, ExpressionRef expression_);
        ParameterAttributes attributes;
      };
      
      enum ExpressionType {
        expression_phi,
        expression_call,
        expression_name,
        expression_function_type,
        expression_exists,
        expression_literal
      };
      
      struct Expression : Element {
        Expression(PSI_MOVE_REF(Expression) src);
        Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_);
        virtual ~Expression();
        virtual Expression* clone() const = 0;
        
        friend Expression* clone(const Expression& expr) {
          return expr.clone();
        }

        ExpressionType expression_type;
      };

      struct NameExpression : Expression {
        NameExpression(const PhysicalSourceLocation& location_, Token name);
        virtual Expression* clone() const;

        Token name;
      };

      struct PhiNode : Element {
        PhiNode(const PhysicalSourceLocation& location_, Maybe<Token> label_, ExpressionRef expression_);

        Maybe<Token> label;
        ExpressionRef expression;
      };

      struct PhiExpression : Expression {
        PhiExpression(const PhysicalSourceLocation& location_, ExpressionRef type_, PSI_STD::vector<PhiNode> nodes_);
        virtual Expression* clone() const;

        ExpressionRef type;
        PSI_STD::vector<PhiNode> nodes;
      };

      struct CallExpression : Expression {
        CallExpression(const PhysicalSourceLocation& location_, Token target_, PSI_STD::vector<ExpressionRef> terms_);
        virtual Expression* clone() const;

        Token target;
        PSI_STD::vector<ExpressionRef> terms;
      };

      struct FunctionTypeExpression : Expression {
        FunctionTypeExpression(PSI_MOVE_REF(FunctionTypeExpression) src);
        FunctionTypeExpression(const PhysicalSourceLocation& location_,
                               CallingConvention calling_convention_,
                               bool sret_,
                               PSI_STD::vector<ParameterExpression> phantom_parameters_,
                               PSI_STD::vector<ParameterExpression> parameters_,
                               ParameterAttributes result_attributes_,
                               ExpressionRef result_type_);
        virtual Expression* clone() const;

        CallingConvention calling_convention;
        bool sret;
        PSI_STD::vector<ParameterExpression> phantom_parameters;
        PSI_STD::vector<ParameterExpression> parameters;
        ParameterAttributes result_attributes;
        ExpressionRef result_type;
      };
      
      struct ExistsExpression : Expression {
        ExistsExpression(const PhysicalSourceLocation& location_,
                         PSI_STD::vector<ParameterExpression> parameters_,
                         ExpressionRef result_);
        virtual Expression* clone() const;

        PSI_STD::vector<ParameterExpression> parameters;
        ExpressionRef result;
      };
      
      enum LiteralType {
        literal_byte,
        literal_ubyte,
        literal_short,
        literal_ushort,
        literal_int,
        literal_uint,
        literal_long,
        literal_ulong,
        literal_quad,
        literal_uquad,
        literal_intptr,
        literal_uintptr
      };

      struct IntegerLiteralExpression : Expression {
        IntegerLiteralExpression(const PhysicalSourceLocation& location_, LiteralType literal_type_, BigInteger value_);
        virtual Expression* clone() const;

        LiteralType literal_type;
        BigInteger value;
      };

      struct Block : Element {
        Block(const PhysicalSourceLocation& location_, bool landing_pad_, Maybe<Token> name_, Maybe<Token> dominator_name_, PSI_STD::vector<NamedExpression> statements_);

        bool landing_pad;
        Maybe<Token> name;
        Maybe<Token> dominator_name;
        PSI_STD::vector<NamedExpression> statements;
      };

      enum GlobalType {
        global_function,
        global_define,
        global_variable,
        global_recursive
      };
      
      struct GlobalElement;
      typedef ClonePtr<GlobalElement> GlobalElementRef;

      struct GlobalElement : Element {
        GlobalElement(const PhysicalSourceLocation& location_, GlobalType global_type_);
        virtual ~GlobalElement();
        virtual GlobalElement* clone() const = 0;
        
        friend GlobalElement* clone(GlobalElement& el) {
          return el.clone();
        }

        GlobalType global_type;
      };
      
      struct Function : GlobalElement {
        Function(const PhysicalSourceLocation& location_,
                 Linkage linkage_,
                 FunctionTypeExpression type_);
        Function(const PhysicalSourceLocation& location_,
                 Linkage linkage_,
                 FunctionTypeExpression type_,
                 PSI_STD::vector<Block> blocks_);
        virtual GlobalElement* clone() const;

        Linkage linkage;
        FunctionTypeExpression type;
        PSI_STD::vector<Block> blocks;
      };

      struct RecursiveType : GlobalElement {
        RecursiveType(const PhysicalSourceLocation& location_,
                      PSI_STD::vector<ParameterExpression> phantom_parameters_,
                      PSI_STD::vector<ParameterExpression> parameters_,
                      ExpressionRef result_);
        virtual GlobalElement* clone() const;

        PSI_STD::vector<ParameterExpression> phantom_parameters;
        PSI_STD::vector<ParameterExpression> parameters;
        ExpressionRef result;
      };

      struct GlobalVariable : GlobalElement {
        GlobalVariable(const PhysicalSourceLocation& location_,
                       bool constant_,
                       Linkage linkage_,
                       ExpressionRef type_);
        GlobalVariable(const PhysicalSourceLocation& location_,
                       bool constant_,
                       Linkage linkage_,
                       ExpressionRef type_,
                       ExpressionRef value_);
        virtual GlobalElement* clone() const;

        bool constant;
        Linkage linkage;
        ExpressionRef type;
        ExpressionRef value;
      };

      struct GlobalDefine : GlobalElement {
        GlobalDefine(const PhysicalSourceLocation& location_, ExpressionRef value_);
        virtual GlobalElement* clone() const;

        ExpressionRef value;
      };

      struct NamedGlobalElement : Element {
        NamedGlobalElement(const PhysicalSourceLocation& location_, Token name_, GlobalElementRef value_);

        Token name;
        GlobalElementRef value;
      };

      /**
       * \brief Checks if a character is a "token" character.
       *
       * A token character is alphanumeric or underscore, so this is
       * equivalent the following in the C locale:
       *
       * \code isalpha(c) || isdigit(c) || c == '_' || (c == '-') || (c == '%') || (c == '.') \endcode
       */
      inline bool token_char(char c) {
        return ((c >= 'A') && (c <= 'Z')) ||
          ((c >= 'a') && (c <= 'z')) ||
          ((c >= '0') && (c <= '9')) ||
          (c == '_') || (c == '-') || (c == '.');
      }
    }

    // Exported for the parser test suite
    PSI_TVM_EXPORT PSI_STD::vector<Parser::NamedGlobalElement> parse(CompileErrorContext& error_context, const SourceLocation& loc, const char *begin, const char *end);
    PSI_TVM_EXPORT PSI_STD::vector<Parser::NamedGlobalElement> parse(CompileErrorContext& error_context, const SourceLocation& loc, const char *begin);
  }
}

#endif
