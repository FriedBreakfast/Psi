#include "Assembler.hpp"

#include "Function.hpp"
#include "Functional.hpp"

#include <cstring>
#include <stdexcept>
#include <boost/next_prior.hpp>

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

      Term* AssemblerContext::get(const std::string& name) const {
	for (const AssemblerContext *self = this; self; self = self->m_parent) {
	  TermMap::const_iterator it = self->m_terms.find(name);
	  if (it != self->m_terms.end())
	    return it->second;
	}
	throw std::logic_error("Name not defined: " + name);
      }

      void AssemblerContext::put(const std::string& name, Term* value) {
	std::pair<TermMap::iterator, bool> result = m_terms.insert(std::make_pair(name, value));
	if (!result.second)
	  throw std::logic_error("Name defined twice: " + name);
      }

      FunctionalTerm* build_functional_expression(AssemblerContext& context, const Parser::CallExpression& expression) {
        std::tr1::unordered_map<std::string, FunctionalTermCallback>::const_iterator it =
          functional_ops.find(expression.target->text);

        if (it == functional_ops.end())
          throw std::logic_error("unknown operation " + expression.target->text);

        return it->second(it->first, context, expression);
      }

      Term* build_expression(AssemblerContext& context, const Parser::Expression& expression) {
	switch(expression.expression_type) {
	case Parser::expression_call:
	  return build_functional_expression(context, checked_cast<const Parser::CallExpression&>(expression));

	case Parser::expression_name:
	  return context.get(checked_cast<const Parser::NameExpression&>(expression).name->text);

	case Parser::expression_function_type:
	  return build_function_type(context, checked_cast<const Parser::FunctionTypeExpression&>(expression));

	default:
	  throw std::logic_error("invalid expression type");
	}
      }

      void build_function_parameters(AssemblerContext& context,
                                     const UniqueList<Parser::NamedExpression>& parameters,
                                     ArrayPtr<FunctionTypeParameterTerm*>  result) {
        std::size_t n = 0;
        for (UniqueList<Parser::NamedExpression>::const_iterator it = parameters.begin();
             it != parameters.end(); ++it, ++n) {
	  Term* param_type = build_expression(context, *it->expression);
	  FunctionTypeParameterTerm* param = context.context().new_function_type_parameter(param_type);
          if (it->name)
            context.put(it->name->text, param);
	  result[n] = param;
        }
      }

      FunctionTypeTerm* build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type) {
	AssemblerContext my_context(&context);

        ScopedTermPtrArray<FunctionTypeParameterTerm> phantom_parameters(function_type.phantom_parameters.size());
        build_function_parameters(my_context, function_type.phantom_parameters, phantom_parameters.array());

        ScopedTermPtrArray<FunctionTypeParameterTerm> parameters(function_type.parameters.size());
        build_function_parameters(my_context, function_type.parameters, parameters.array());

	Term* result_type = build_expression(my_context, *function_type.result_type);

	return context.context().get_function_type(function_type.calling_convention, result_type, phantom_parameters.array(), parameters.array());
      }

      Term* build_instruction_expression(AssemblerContext& context, BlockTerm& block, const Parser::CallExpression& expression) {
        std::tr1::unordered_map<std::string, InstructionTermCallback>::const_iterator it =
          instruction_ops.find(expression.target->text);

        if (it != instruction_ops.end()) {
          return it->second(it->first, block, context, expression);
        } else {
          return build_functional_expression(context, expression);
        }
      }

      Term* build_instruction(AssemblerContext& context, BlockTerm& block, const Parser::Expression& expression) {
        switch(expression.expression_type) {
	case Parser::expression_phi:
          throw std::logic_error("not implemented");

	case Parser::expression_call:
          return build_instruction_expression(context, block, checked_cast<const Parser::CallExpression&>(expression));

        default:
          return build_expression(context, expression);
        }
      }

      void build_function(AssemblerContext& context, FunctionTerm& function, const Parser::Function& function_def) {
        AssemblerContext my_context(&context);

        std::vector<BlockTerm*> blocks;

        BlockTerm* entry = function.new_block();
        function.set_entry(entry);
        blocks.push_back(entry);

        std::size_t n = 0;
        for (UniqueList<Parser::NamedExpression>::const_iterator it = function_def.type->phantom_parameters.begin();
             it != function_def.type->phantom_parameters.end(); ++it, ++n) {
          if (it->name)
            my_context.put(it->name->text, function.parameter(n));
        }

        for (UniqueList<Parser::NamedExpression>::const_iterator it = function_def.type->parameters.begin();
             it != function_def.type->parameters.end(); ++it, ++n) {
          if (it->name)
            my_context.put(it->name->text, function.parameter(n));
        }

        for (UniqueList<Parser::Block>::const_iterator it = boost::next(function_def.blocks.begin());
             it != function_def.blocks.end(); ++it) {
          BlockTerm* dominator;
          if (it->dominator_name) {
            Term* dominator_base = my_context.get(it->dominator_name->text);
            if (dominator_base->term_type() != term_block)
              throw std::logic_error("dominator block name is not a block");
            dominator = checked_cast<BlockTerm*>(dominator_base);
          } else {
            dominator = entry;
          }
          BlockTerm* bl = function.new_block(dominator);
          my_context.put(it->name->text, bl);
          blocks.push_back(bl);
        }

        std::vector<BlockTerm*>::const_iterator bt = blocks.begin();
        for (UniqueList<Parser::Block>::const_iterator it = function_def.blocks.begin();
             it != function_def.blocks.end(); ++it, ++bt) {
          PSI_ASSERT(bt != blocks.end());
          for (UniqueList<Parser::NamedExpression>::const_iterator jt = it->statements.begin();
               jt != it->statements.end(); ++jt) {
            Term* value = build_instruction(my_context, **bt, *jt->expression);
            if (jt->name)
              my_context.put(jt->name->text, value);
          }
        }
      }
    }

    AssemblerResult build(Context& context, const boost::intrusive::list<Parser::NamedGlobalElement>& globals) {
      Assembler::AssemblerContext asmct(&context);
      AssemblerResult result;

      for (UniqueList<Parser::NamedGlobalElement>::const_iterator it = globals.begin();
	   it != globals.end(); ++it) {
	if (it->value->global_type == Parser::global_function) {
          const Parser::Function& def = checked_cast<const Parser::Function&>(*it->value);
          FunctionTypeTerm* function_type = Assembler::build_function_type(asmct, *def.type);
          FunctionTerm* function = context.new_function(function_type);
	  asmct.put(it->name->text, function);
	  result[it->name->text] = function;
	} else if (it->value->global_type == Parser::global_variable) {
          const Parser::GlobalVariable& var = checked_cast<const Parser::GlobalVariable&>(*it->value);
          Term* global_type = Assembler::build_expression(asmct, *var.type);
          GlobalVariableTerm* global_var = context.new_global_variable(global_type, var.constant);
          asmct.put(it->name->text, global_var);
          result[it->name->text] = global_var;
	} else {
          const Parser::GlobalDefine& def = checked_cast<const Parser::GlobalDefine&>(*it->value);
	  PSI_ASSERT(it->value->global_type == Parser::global_define);
          Term* ptr = Assembler::build_expression(asmct, *def.value);
          asmct.put(it->name->text, ptr);
	}
      }

      for (boost::intrusive::list<Parser::NamedGlobalElement>::const_iterator it = globals.begin();
	   it != globals.end(); ++it) {
        if (it->value->global_type == Parser::global_define)
          continue;

	GlobalTerm* ptr = result[it->name->text];
	PSI_ASSERT(ptr);
	if (it->value->global_type == Parser::global_function) {
          const Parser::Function& def = checked_cast<const Parser::Function&>(*it->value);
          FunctionTerm* function = checked_cast<FunctionTerm*>(ptr);
          if (!def.blocks.empty())
            Assembler::build_function(asmct, *function, def);
	} else {
          PSI_ASSERT(it->value->global_type == Parser::global_variable);
          const Parser::GlobalVariable& var = checked_cast<const Parser::GlobalVariable&>(*it->value);
          GlobalVariableTerm* global_var = checked_cast<GlobalVariableTerm*>(ptr);
          Term* value = Assembler::build_expression(asmct, *var.value);
          global_var->set_value(value);
	}
      }

      return result;
    }

    AssemblerResult parse_and_build(Context& context, const char *begin, const char *end) {
      UniqueList<Parser::NamedGlobalElement> globals;
      parse(globals, begin, end);
      return build(context, globals);
    }

    AssemblerResult parse_and_build(Context& context, const char *begin) {
      return parse_and_build(context, begin, begin+std::strlen(begin));
    }
  }
}
