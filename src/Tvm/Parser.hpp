#ifndef HPP_PSI_TVM_PARSER
#define HPP_PSI_TVM_PARSER

#include <string>

#include <boost/checked_delete.hpp>
#include <boost/intrusive/list.hpp>

#include "Core.hpp"

namespace Psi {
  namespace Tvm {
    template<typename T>
    class UniqueList : public boost::intrusive::list<T> {
    public:
      ~UniqueList() {
	clear_and_dispose(boost::checked_deleter<T>());
      }
    };

    namespace Parser {
      struct Location {
	int first_line;
	int first_column;
	int last_line;
	int last_column;
      };

      struct Element {
	Element(const Location& location_);

	Location location;
      };

      struct Token : Element, boost::intrusive::list_base_hook<> {
	Token(const Location& location_, const std::string& text_);

	std::string text;
      };

      struct Expression;

      struct NamedExpression : Element, boost::intrusive::list_base_hook<> {
	NamedExpression(const Location& location_, UniquePtr<Token>& name_, UniquePtr<Expression>& expression_);
	NamedExpression(const Location& location_, UniquePtr<Expression>& expression_);
	virtual ~NamedExpression();

	UniquePtr<Token> name;
	UniquePtr<Expression> expression;
      };

      enum ExpressionType {
	expression_phi,
	expression_call,
	expression_name,
	expression_function_type
      };

      struct Expression : Element, boost::intrusive::list_base_hook<> {
	Expression(const Location& location_, ExpressionType expression_type_);
	virtual ~Expression();

	ExpressionType expression_type;
      };

      struct NameExpression : Expression {
	NameExpression(const Location& location_, UniquePtr<Token>& name);
	virtual ~NameExpression();

	UniquePtr<Token> name;
      };

      struct PhiNode : Element, boost::intrusive::list_base_hook<> {
	PhiNode(const Location& location_, UniquePtr<Token>& label_, UniquePtr<Expression>& expression_);
	~PhiNode();

	UniquePtr<Token> label;
	UniquePtr<Expression> expression;
      };

      struct PhiExpression : Expression {
	PhiExpression(const Location& location_, UniquePtr<Expression>& type_, UniqueList<PhiNode>& nodes);
	virtual ~PhiExpression();

	UniquePtr<Expression> type;
	UniqueList<PhiNode> nodes;
      };

      struct CallExpression : Expression {
	CallExpression(const Location& location_, UniquePtr<Token>& target_,
		       UniqueList<Token>& values_, UniqueList<Expression>& terms_);
	virtual ~CallExpression();

	UniquePtr<Token> target;
	UniqueList<Token> values;
	UniqueList<Expression> terms;
      };

      struct FunctionTypeExpression : Expression {
	FunctionTypeExpression(const Location& location_,
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

      struct Block : Element, boost::intrusive::list_base_hook<> {
	Block(const Location& location_, UniqueList<NamedExpression>& statements_);
	Block(const Location& location_, UniquePtr<Token>& name_, UniquePtr<Token>& dominator_name_,
              UniqueList<NamedExpression>& statements_);
	~Block();

	UniquePtr<Token> name;
        UniquePtr<Token> dominator_name;
	UniqueList<NamedExpression> statements;
      };

      enum GlobalType {
	global_function,
	global_define,
	global_variable
      };

      struct GlobalElement : Element, boost::intrusive::list_base_hook<> {
	GlobalElement(const Location& location_, GlobalType global_type_);
	virtual ~GlobalElement();

	GlobalType global_type;
      };

      struct Function : GlobalElement {
	Function(const Location& location_,
		 UniquePtr<FunctionTypeExpression>& type_);
	Function(const Location& location_,
		 UniquePtr<FunctionTypeExpression>& type_,
		 UniqueList<Block>& blocks_);
	virtual ~Function();

	UniquePtr<FunctionTypeExpression> type;
	UniqueList<Block> blocks;
      };

      struct GlobalVariable : GlobalElement {
	GlobalVariable(const Location& location_,
                       bool constant_,
		       UniquePtr<Expression>& type_);
	GlobalVariable(const Location& location_,
                       bool constant_,
		       UniquePtr<Expression>& type_,
		       UniquePtr<Expression>& value_);
	virtual ~GlobalVariable();

        bool constant;
	UniquePtr<Expression> type;
	UniquePtr<Expression> value;
      };

      struct GlobalDefine : GlobalElement {
	GlobalDefine(const Location& location_,
		     UniquePtr<Expression>& value_);
	virtual ~GlobalDefine();

	UniquePtr<Expression> value;
      };

      struct NamedGlobalElement : Element, boost::intrusive::list_base_hook<> {
	NamedGlobalElement(const Location& location_, UniquePtr<Token>& name_, UniquePtr<GlobalElement>& value_);
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
