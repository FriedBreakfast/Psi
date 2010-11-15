#include "Assembler.hpp"

#include "Function.hpp"

#include <cstring>
#include <stdexcept>

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      AssemblerContext::AssemblerContext(Context *context) : m_context(context), m_parent(0) {
      }

      AssemblerContext::AssemblerContext(const AssemblerContext *parent) : m_context(parent->m_context), m_parent(parent) {
      }

      Context& AssemblerContext::context() const {
	return *m_context;
      }

      const TermPtr<>& AssemblerContext::get(const std::string& name) const {
	for (const AssemblerContext *self = this; self; self = self->m_parent) {
	  std::map<std::string, TermPtr<> >::const_iterator it = self->terms.find(name);
	  if (it != self->terms.end())
	    return it->second;
	}
	throw std::logic_error("Name not defined: " + name);
      }

      void AssemblerContext::put(const std::string& name, const TermPtr<>& value) {
	std::pair<std::map<std::string, TermPtr<> >::iterator, bool> result =
	  terms.insert(std::make_pair(name, value));
	if (!result.second)
	  throw std::logic_error("Name defined twice: " + name);
      }

      TermPtr<> build_call_expression(AssemblerContext& context, const Parser::CallExpression& expression) {
      }

      TermPtr<> build_expression(AssemblerContext& context, const Parser::Expression& expression) {
	switch(expression.expression_type) {
	case Parser::expression_call:
	  return build_call_expression(context, checked_cast<const Parser::CallExpression&>(expression));

	case Parser::expression_name:
	  return context.get(checked_cast<const Parser::NameExpression&>(expression).name->text);

	case Parser::expression_function_type:
	  return build_function_type(context, checked_cast<const Parser::FunctionTypeExpression&>(expression));

	default:
	  throw std::logic_error("invalid expression type");
	}
      }

      TermPtr<FunctionTypeTerm> build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type) {
	AssemblerContext my_context(&context);
	TermPtrArray<FunctionTypeParameterTerm> parameters(function_type.parameters.size());

	std::size_t n = 0;
	for (UniqueList<Parser::NamedExpression>::const_iterator it = function_type.parameters.begin();
	     it != function_type.parameters.end(); ++it, ++n) {
	  PSI_ASSERT(n < parameters.size());
	  TermPtr<> param_type = build_expression(context, *it->expression);
	  TermPtr<FunctionTypeParameterTerm> param =
	    context.context().new_function_type_parameter(param_type);
	  my_context.put(it->name->text, param);
	  parameters.set(n, param);
	}

	TermPtr<> result_type = build_expression(context, *function_type.result_type);

	return context.context().get_function_type(cconv_tvm, result_type, parameters);
      }

      TermPtr<FunctionTerm> build_function(AssemblerContext& context, const Parser::Function& function) {
	TermPtr<FunctionTypeTerm> function_type = build_function_type(context, *function.type);
	return context.context().new_function(function_type);
      }
    }

    std::map<std::string, TermPtr<GlobalTerm> >
    build(Context& context, const boost::intrusive::list<Parser::NamedGlobalElement>& globals) {
      Assembler::AssemblerContext asmct(&context);
      std::map<std::string, TermPtr<GlobalTerm> > result;

      for (UniqueList<Parser::NamedGlobalElement>::const_iterator it = globals.begin();
	   it != globals.end(); ++it) {
	if (it->value->global_type == Parser::global_function) {
	  TermPtr<FunctionTerm> ptr = Assembler::build_function(asmct, checked_cast<const Parser::Function&>(*it->value));
	  asmct.put(it->name->text, ptr);
	  result[it->name->text] = ptr;
	} else if (it->value->global_type == Parser::global_variable) {
	} else {
	  PSI_ASSERT(it->value->global_type == Parser::global_define);
	}
      }

      for (boost::intrusive::list<Parser::NamedGlobalElement>::const_iterator it = globals.begin();
	   it != globals.end(); ++it) {
	const TermPtr<GlobalTerm>& ptr = result[it->name->text];
	PSI_ASSERT(ptr);
	if (it->value->global_type == Parser::global_function) {
	} else if (it->value->global_type == Parser::global_variable) {
	}
      }

      return result;
    }

    std::map<std::string, TermPtr<GlobalTerm> >
    parse_and_build(Context& context, const char *begin, const char *end) {
      UniqueList<Parser::NamedGlobalElement> globals;
      parse(globals, begin, end);
      return build(context, globals);
    }

    std::map<std::string, TermPtr<GlobalTerm> >
    parse_and_build(Context& context, const char *begin) {
      return parse_and_build(context, begin, begin+std::strlen(begin));
    }
  }
}
