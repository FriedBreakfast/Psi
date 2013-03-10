#ifndef HPP_PSI_TVM_PARSER
#define HPP_PSI_TVM_PARSER

#include <string>

#include "Core.hpp"
#include "ParserUtility.hpp"
#include "../SourceLocation.hpp"

namespace Psi {
  namespace Tvm {
    namespace Parser {
      struct Element {
        Element(const PhysicalSourceLocation& location_);

        PhysicalSourceLocation location;
      };

      struct Token : Element, boost::intrusive::list_base_hook<> {
        Token(const PhysicalSourceLocation& location_, const std::string& text_);

        std::string text;
      };

      struct Expression;

      struct NamedExpression : Element, boost::intrusive::list_base_hook<> {
        NamedExpression(const PhysicalSourceLocation& location_, UniquePtr<Token>& name_, UniquePtr<Expression>& expression_);
        NamedExpression(const PhysicalSourceLocation& location_, UniquePtr<Expression>& expression_);

        UniquePtr<Token> name;
        UniquePtr<Expression> expression;
      };

      enum ExpressionType {
        expression_phi,
        expression_call,
        expression_name,
        expression_function_type,
        expression_exists,
        expression_literal
      };

      struct Expression : Element, boost::intrusive::list_base_hook<> {
        Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_);
        virtual ~Expression();

        ExpressionType expression_type;
      };

      struct NameExpression : Expression {
        NameExpression(const PhysicalSourceLocation& location_, UniquePtr<Token>& name);

        UniquePtr<Token> name;
      };

      struct PhiNode : Element, boost::intrusive::list_base_hook<> {
        PhiNode(const PhysicalSourceLocation& location_, UniquePtr<Token>& label_, UniquePtr<Expression>& expression_);

        UniquePtr<Token> label;
        UniquePtr<Expression> expression;
      };

      struct PhiExpression : Expression {
        PhiExpression(const PhysicalSourceLocation& location_, UniquePtr<Expression>& type_, UniqueList<PhiNode>& nodes);

        UniquePtr<Expression> type;
        UniqueList<PhiNode> nodes;
      };

      struct CallExpression : Expression {
        CallExpression(const PhysicalSourceLocation& location_, UniquePtr<Token>& target_,
                       UniqueList<Expression>& terms_);

        UniquePtr<Token> target;
        UniqueList<Expression> terms;
      };

      struct FunctionTypeExpression : Expression {
        FunctionTypeExpression(const PhysicalSourceLocation& location_,
                               CallingConvention calling_convention_,
                               bool sret_,
                               UniqueList<NamedExpression>& phantom_parameters_,
                               UniqueList<NamedExpression>& parameters_,
                               UniquePtr<Expression>& result_type_);

        CallingConvention calling_convention;
        bool sret;
        UniqueList<NamedExpression> phantom_parameters;
        UniqueList<NamedExpression> parameters;
        UniquePtr<Expression> result_type;
      };
      
      struct ExistsExpression : Expression {
        ExistsExpression(const PhysicalSourceLocation& location_,
                         UniqueList<NamedExpression>& parameters_,
                         UniquePtr<Expression>& result_);
        
        UniqueList<NamedExpression> parameters;
        UniquePtr<Expression> result;
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

      struct LiteralExpression : Expression {
        LiteralExpression(const PhysicalSourceLocation& location_, LiteralType literal_type_, UniquePtr<Token>& value_);

        LiteralType literal_type;
        UniquePtr<Token> value;
      };

      struct Block : Element, boost::intrusive::list_base_hook<> {
        Block(const PhysicalSourceLocation& location_, bool landing_pad_, UniqueList<NamedExpression>& statements_);
        Block(const PhysicalSourceLocation& location_, bool landing_pad_, UniquePtr<Token>& name_, UniquePtr<Token>& dominator_name_,
              UniqueList<NamedExpression>& statements_);

        UniquePtr<Token> name;
        UniquePtr<Token> dominator_name;
        UniqueList<NamedExpression> statements;
        bool landing_pad;
      };

      enum GlobalType {
        global_function,
        global_define,
        global_variable,
        global_recursive
      };

      struct GlobalElement : Element, boost::intrusive::list_base_hook<> {
        GlobalElement(const PhysicalSourceLocation& location_, GlobalType global_type_);
        virtual ~GlobalElement();

        GlobalType global_type;
      };

      struct Function : GlobalElement {
        Function(const PhysicalSourceLocation& location_,
                 UniquePtr<FunctionTypeExpression>& type_);
        Function(const PhysicalSourceLocation& location_,
                 UniquePtr<FunctionTypeExpression>& type_,
                 UniqueList<Block>& blocks_);

        UniquePtr<FunctionTypeExpression> type;
        UniqueList<Block> blocks;
      };

      struct RecursiveType : GlobalElement {
        RecursiveType(const PhysicalSourceLocation& location_,
                      UniqueList<NamedExpression>& phantom_parameters_,
                      UniqueList<NamedExpression>& parameters_,
                      UniquePtr<Expression>& result_);

        UniqueList<NamedExpression> phantom_parameters;
        UniqueList<NamedExpression> parameters;
        UniquePtr<Expression> result;
      };

      struct GlobalVariable : GlobalElement {
        GlobalVariable(const PhysicalSourceLocation& location_,
                       bool constant_,
                       UniquePtr<Expression>& type_);
        GlobalVariable(const PhysicalSourceLocation& location_,
                       bool constant_,
                       UniquePtr<Expression>& type_,
                       UniquePtr<Expression>& value_);

        bool constant;
        UniquePtr<Expression> type;
        UniquePtr<Expression> value;
      };

      struct GlobalDefine : GlobalElement {
        GlobalDefine(const PhysicalSourceLocation& location_,
                     UniquePtr<Expression>& value_);

        UniquePtr<Expression> value;
      };

      struct NamedGlobalElement : Element, boost::intrusive::list_base_hook<> {
        NamedGlobalElement(const PhysicalSourceLocation& location_, UniquePtr<Token>& name_, UniquePtr<GlobalElement>& value_);

        UniquePtr<Token> name;
        UniquePtr<GlobalElement> value;
      };

      /**
       * \brief Checks if a character is a "token" character.
       *
       * A token character is alphanumeric or underscore, so this is
       * equivalent the following in the C locale:
       *
       * \code isalpha(c) || isdigit(c) || c == '_' || (c == '-') || (c == '%') \endcode
       */
      inline bool token_char(char c) {
        return ((c >= 'A') && (c <= 'Z')) ||
          ((c >= 'a') && (c <= 'z')) ||
          ((c >= '0') && (c <= '9')) ||
          (c == '_') || (c == '-') || (c == '\\');
      }
    }

    void parse(UniqueList<Parser::NamedGlobalElement>& result, const char *begin, const char *end);
    void parse(UniqueList<Parser::NamedGlobalElement>& result, const char *begin);
  }
}

#endif
