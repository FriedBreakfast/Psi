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
        virtual ~NamedExpression();

        UniquePtr<Token> name;
        UniquePtr<Expression> expression;
      };

      enum ExpressionType {
        expression_phi,
        expression_call,
        expression_name,
        expression_function_type,
        expression_literal
      };

      struct Expression : Element, boost::intrusive::list_base_hook<> {
        Expression(const PhysicalSourceLocation& location_, ExpressionType expression_type_);
        virtual ~Expression();

        ExpressionType expression_type;
      };

      struct NameExpression : Expression {
        NameExpression(const PhysicalSourceLocation& location_, UniquePtr<Token>& name);
        virtual ~NameExpression();

        UniquePtr<Token> name;
      };

      struct PhiNode : Element, boost::intrusive::list_base_hook<> {
        PhiNode(const PhysicalSourceLocation& location_, UniquePtr<Token>& label_, UniquePtr<Expression>& expression_);
        ~PhiNode();

        UniquePtr<Token> label;
        UniquePtr<Expression> expression;
      };

      struct PhiExpression : Expression {
        PhiExpression(const PhysicalSourceLocation& location_, UniquePtr<Expression>& type_, UniqueList<PhiNode>& nodes);
        virtual ~PhiExpression();

        UniquePtr<Expression> type;
        UniqueList<PhiNode> nodes;
      };

      struct CallExpression : Expression {
        CallExpression(const PhysicalSourceLocation& location_, UniquePtr<Token>& target_,
                       UniqueList<Expression>& terms_);
        virtual ~CallExpression();

        UniquePtr<Token> target;
        UniqueList<Expression> terms;
      };

      struct FunctionTypeExpression : Expression {
        FunctionTypeExpression(const PhysicalSourceLocation& location_,
                               CallingConvention calling_convention_,
                               UniqueList<NamedExpression>& phantom_parameters_,
                               UniqueList<NamedExpression>& parameters_,
                               UniquePtr<Expression>& result_type_);
        virtual ~FunctionTypeExpression();

        CallingConvention calling_convention;
        UniqueList<NamedExpression> phantom_parameters;
        UniqueList<NamedExpression> parameters;
        UniquePtr<Expression> result_type;
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
        virtual ~LiteralExpression();

        LiteralType literal_type;
        UniquePtr<Token> value;
      };

      struct Block : Element, boost::intrusive::list_base_hook<> {
        Block(const PhysicalSourceLocation& location_, bool landing_pad_, UniqueList<NamedExpression>& statements_);
        Block(const PhysicalSourceLocation& location_, bool landing_pad_, UniquePtr<Token>& name_, UniquePtr<Token>& dominator_name_,
              UniqueList<NamedExpression>& statements_);
        ~Block();

        UniquePtr<Token> name;
        UniquePtr<Token> dominator_name;
        UniqueList<NamedExpression> statements;
        bool landing_pad;
      };

      enum GlobalType {
        global_function,
        global_define,
        global_variable
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
        virtual ~Function();

        UniquePtr<FunctionTypeExpression> type;
        UniqueList<Block> blocks;
      };

      struct GlobalVariable : GlobalElement {
        GlobalVariable(const PhysicalSourceLocation& location_,
                       bool constant_,
                       UniquePtr<Expression>& type_);
        GlobalVariable(const PhysicalSourceLocation& location_,
                       bool constant_,
                       UniquePtr<Expression>& type_,
                       UniquePtr<Expression>& value_);
        virtual ~GlobalVariable();

        bool constant;
        UniquePtr<Expression> type;
        UniquePtr<Expression> value;
      };

      struct GlobalDefine : GlobalElement {
        GlobalDefine(const PhysicalSourceLocation& location_,
                     UniquePtr<Expression>& value_);
        virtual ~GlobalDefine();

        UniquePtr<Expression> value;
      };

      struct NamedGlobalElement : Element, boost::intrusive::list_base_hook<> {
        NamedGlobalElement(const PhysicalSourceLocation& location_, UniquePtr<Token>& name_, UniquePtr<GlobalElement>& value_);
        ~NamedGlobalElement();

        UniquePtr<Token> name;
        UniquePtr<GlobalElement> value;
      };
    }

    void parse(UniqueList<Parser::NamedGlobalElement>& result, const char *begin, const char *end);
    void parse(UniqueList<Parser::NamedGlobalElement>& result, const char *begin);
  }
}

#endif
