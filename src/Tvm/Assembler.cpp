#include "Assembler.hpp"

#include "Function.hpp"
#include "Functional.hpp"
#include "FunctionalBuilder.hpp"
#include "Recursive.hpp"
#include "Number.hpp"

#include <cstring>

#include <boost/next_prior.hpp>

namespace Psi {
  namespace Tvm {
    namespace Assembler {
      AssemblerError::AssemblerError(const std::string& msg) {
        m_message = "Psi TVM assembler error: ";
        m_message += msg;
        m_str = m_message.c_str();
      }

      AssemblerError::~AssemblerError() throw () {
      }

      const char* AssemblerError::what() const throw() {
        return m_str;
      }

      AssemblerContext::AssemblerContext(Module *module) : m_module(module), m_parent(0) {
      }

      AssemblerContext::AssemblerContext(const AssemblerContext *parent) : m_module(parent->m_module), m_parent(parent) {
      }
      
      Module& AssemblerContext::module() const {
        return *m_module;
      }

      Context& AssemblerContext::context() const {
        return module().context();
      }
      
      /**
       * Get error handling context.
       * 
       * This calls \code context().error_context() \endcode
       */
      CompileErrorContext& AssemblerContext::error_context() const {
        return context().error_context();
      }

      ValuePtr<> AssemblerContext::get(const std::string& name) const {
        for (const AssemblerContext *self = this; self; self = self->m_parent) {
          TermMap::const_iterator it = self->m_terms.find(name);
          if (it != self->m_terms.end())
            return it->second;
        }
        throw AssemblerError("Name not defined: " + name);
      }

      void AssemblerContext::put(const std::string& name, const ValuePtr<>& value) {
        std::pair<TermMap::iterator, bool> result = m_terms.insert(std::make_pair(name, value));
        if (!result.second)
          throw AssemblerError("Name defined twice: " + name);
      }

      ValuePtr<> build_functional_expression(AssemblerContext& context, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& logical_location) {
        boost::unordered_map<std::string, FunctionalTermCallback>::const_iterator it =
          functional_ops.find(expression.target.text);

        if (it == functional_ops.end())
          throw AssemblerError("unknown operation " + expression.target.text);

        return it->second(it->first, context, expression, logical_location);
      }

      ValuePtr<> build_integer_literal(AssemblerContext& context, const Parser::IntegerLiteralExpression& expression, const LogicalSourceLocationPtr& logical_location) {
        switch (expression.literal_type) {
#define HANDLE_INT(lit_name,int_name)                                   \
          case Parser::literal_##lit_name:                              \
            return FunctionalBuilder::int_value(context.context(), IntegerType::int_name, true, expression.value, SourceLocation(expression.location, logical_location)); \
          case Parser::literal_u##lit_name:                             \
            return FunctionalBuilder::int_value(context.context(), IntegerType::int_name, false, expression.value, SourceLocation(expression.location, logical_location)); \

          HANDLE_INT(byte,  i8)
          HANDLE_INT(short, i16)
          HANDLE_INT(int,   i32)
          HANDLE_INT(long,  i64)
          HANDLE_INT(quad,  i128)
          HANDLE_INT(intptr, iptr)

#undef HANDLE_INT

        default:
          PSI_FAIL("invalid integer literal type");
        }
      }
      
      ValuePtr<> build_exists(AssemblerContext& context, const Parser::ExistsExpression& expression, const LogicalSourceLocationPtr& logical_location) {
        AssemblerContext my_context(&context);

        std::vector<ParameterPlaceholderType> parameters_with_attributes =
          build_parameters(my_context, false, expression.parameters, logical_location);
          
        std::vector<ValuePtr<ParameterPlaceholder> > parameters;
        for (std::vector<ParameterPlaceholderType>::const_iterator ii = parameters_with_attributes.begin(), ie = parameters_with_attributes.end(); ii != ie; ++ii)
          parameters.push_back(ii->value);
          
        ValuePtr<> result = build_expression(my_context, *expression.result, logical_location);

        return context.context().get_exists(result, parameters, SourceLocation(expression.location, logical_location));
      }

      ValuePtr<> build_expression(AssemblerContext& context, const Parser::Expression& expression, const LogicalSourceLocationPtr& logical_location) {
        switch(expression.expression_type) {
        case Parser::expression_call:
          return build_functional_expression(context, checked_cast<const Parser::CallExpression&>(expression), logical_location);

        case Parser::expression_name:
          return context.get(checked_cast<const Parser::NameExpression&>(expression).name.text);

        case Parser::expression_function_type:
          return build_function_type(context, checked_cast<const Parser::FunctionTypeExpression&>(expression), logical_location);

        case Parser::expression_literal:
          return build_integer_literal(context, checked_cast<const Parser::IntegerLiteralExpression&>(expression), logical_location);
          
        case Parser::expression_exists:
          return build_exists(context, checked_cast<const Parser::ExistsExpression&>(expression), logical_location);

        default:
          PSI_FAIL("invalid expression type");
        }
      }

      std::vector<ParameterPlaceholderType> build_parameters(AssemblerContext& context, bool allow_attributes,
                                                             const PSI_STD::vector<Parser::ParameterExpression>& parameters,
                                                             const LogicalSourceLocationPtr& logical_location) {
        std::vector<ParameterPlaceholderType> result;
        for (PSI_STD::vector<Parser::ParameterExpression>::const_iterator it = parameters.begin(); it != parameters.end(); ++it) {
          ValuePtr<> param_type = build_expression(context, *it->expression, logical_location);
          ValuePtr<ParameterPlaceholder> param = context.context().new_placeholder_parameter(param_type, SourceLocation(it->location, logical_location));
          if (it->name)
            context.put(it->name->text, param);
          if (!allow_attributes && it->attributes.flags)
            throw AssemblerError("attributes found in parameter list where they are not allowed");
          result.push_back(ParameterPlaceholderType(param, it->attributes));
        }
        return result;
      }

      ValuePtr<FunctionType> build_function_type(AssemblerContext& context, const Parser::FunctionTypeExpression& function_type, const LogicalSourceLocationPtr& logical_location) {
        AssemblerContext my_context(&context);

        std::vector<ParameterPlaceholderType> phantom_parameters =
          build_parameters(my_context, false, function_type.phantom_parameters, logical_location);
          
        unsigned n_phantom = phantom_parameters.size();

        std::vector<ParameterPlaceholderType> parameters =
          build_parameters(my_context, true, function_type.parameters, logical_location);
          
        parameters.insert(parameters.begin(), phantom_parameters.begin(), phantom_parameters.end());

        ParameterType result_type(build_expression(my_context, *function_type.result_type, logical_location), function_type.result_attributes);

        return context.context().get_function_type(function_type.calling_convention, result_type, parameters, n_phantom, function_type.sret,
                                                   SourceLocation(function_type.location, logical_location));
      }
      
      void build_recursive_parameters(AssemblerContext& context, RecursiveType::ParameterList& output, bool phantom,
                                      const PSI_STD::vector<Parser::ParameterExpression>& parameters, const LogicalSourceLocationPtr& logical_location) {
        for (PSI_STD::vector<Parser::ParameterExpression>::const_iterator it = parameters.begin(), ie = parameters.end(); it != ie; ++it) {
          ValuePtr<> param_type = build_expression(context, *it->expression, logical_location);
          ValuePtr<RecursiveParameter> param = RecursiveParameter::create(param_type, phantom, SourceLocation(it->location, logical_location));
          if (it->name)
            context.put(it->name->text, param);
          output.push_back(param);
        }
      }

      ValuePtr<RecursiveType> build_recursive_type(AssemblerContext& context, const Parser::RecursiveType& recursive_type, const LogicalSourceLocationPtr& logical_location) {
        AssemblerContext my_context(&context);
        
        RecursiveType::ParameterList parameters;
        build_recursive_parameters(my_context, parameters, true, recursive_type.phantom_parameters, logical_location);
        build_recursive_parameters(my_context, parameters, false, recursive_type.parameters, logical_location);
        
        return RecursiveType::create(context.context(), parameters, SourceLocation(recursive_type.location, logical_location));
      }

      ValuePtr<> build_instruction_expression(AssemblerContext& context, InstructionBuilder& builder, const Parser::CallExpression& expression, const LogicalSourceLocationPtr& logical_location) {
        boost::unordered_map<std::string, InstructionTermCallback>::const_iterator it =
          instruction_ops.find(expression.target.text);

        if (it != instruction_ops.end()) {
          return it->second(it->first, builder, context, expression, logical_location);
        } else {
          return build_functional_expression(context, expression, logical_location);
        }
      }

      ValuePtr<> build_instruction(AssemblerContext& context, std::vector<ValuePtr<Phi> >& phi_nodes,
                                   InstructionBuilder& builder, const Parser::Expression& expression,
                                   const LogicalSourceLocationPtr& logical_location) {
        switch(expression.expression_type) {
        case Parser::expression_phi: {
          const Parser::PhiExpression& phi_expr = checked_cast<const Parser::PhiExpression&>(expression);

          // check that all the incoming edges listed are indeed label values
          for (PSI_STD::vector<Parser::PhiNode>::const_iterator kt = phi_expr.nodes.begin();
               kt != phi_expr.nodes.end(); ++kt) {
            if (!kt->label)
              continue; // Jump in from entry block
            ValuePtr<> block = context.get(kt->label->text);
            if (block->term_type() != term_block)
              throw AssemblerError("incoming label of phi node does not name a block");
          }

          ValuePtr<> type = build_expression(context, *phi_expr.type, logical_location);
          ValuePtr<Phi> phi = builder.insert_point().block()->insert_phi(type, SourceLocation(phi_expr.location, logical_location));
          phi_nodes.push_back(phi);
          return phi;
        }

        case Parser::expression_call:
          return build_instruction_expression(context, builder, checked_cast<const Parser::CallExpression&>(expression), logical_location);

        default:
          return build_expression(context, expression, logical_location);
        }
      }

      void build_function(AssemblerContext& context, const ValuePtr<Function>& function, const Parser::Function& function_def) {
        AssemblerContext my_context(&context);

        std::vector<ValuePtr<Block> > blocks;
        
        LogicalSourceLocationPtr logical_location = function->location().logical;
        
        ValuePtr<Block> entry = function->new_block(function->location());
        blocks.push_back(entry);

        Function::ParameterList::const_iterator param = function->parameters().begin();
        for (PSI_STD::vector<Parser::ParameterExpression>::const_iterator it = function_def.type.phantom_parameters.begin();
             it != function_def.type.phantom_parameters.end(); ++it, ++param) {
          if (it->name) {
            my_context.put(it->name->text, *param);
            function->add_term_name(*param, it->name->text);
          }
        }

        for (PSI_STD::vector<Parser::ParameterExpression>::const_iterator it = function_def.type.parameters.begin();
             it != function_def.type.parameters.end(); ++it, ++param) {
          if (it->name) {
            my_context.put(it->name->text, *param);
            function->add_term_name(*param, it->name->text);
          }
        }

        for (PSI_STD::vector<Parser::Block>::const_iterator it = boost::next(function_def.blocks.begin());
             it != function_def.blocks.end(); ++it) {
          PSI_ASSERT(it->name); // All blocks except the entry block must be named
          ValuePtr<Block> dominator;
          if (it->dominator_name) {
            ValuePtr<> dominator_base = my_context.get(it->dominator_name->text);
            if (dominator_base->term_type() != term_block)
              throw AssemblerError("dominator block name is not a block");
            dominator = value_cast<Block>(dominator_base);
          } else {
            dominator = entry;
          }
          LogicalSourceLocationPtr block_location_logical = logical_location->new_child(it->name->text);
          ValuePtr<Block> bl = function->new_block(SourceLocation(it->location, block_location_logical), dominator);
          my_context.put(it->name->text, bl);
          function->add_term_name(bl, it->name->text);
          blocks.push_back(bl);
        }

        std::vector<ValuePtr<Phi> > phi_nodes;
        std::vector<ValuePtr<Block> >::const_iterator bt = blocks.begin();
        for (PSI_STD::vector<Parser::Block>::const_iterator it = function_def.blocks.begin();
             it != function_def.blocks.end(); ++it, ++bt) {
          PSI_ASSERT(bt != blocks.end());
          InstructionBuilder builder(*bt);
          for (PSI_STD::vector<Parser::NamedExpression>::const_iterator jt = it->statements.begin();
               jt != it->statements.end(); ++jt) {
            LogicalSourceLocationPtr value_location =
              jt->name ? logical_location->new_child(jt->name->text) : logical_location;
            ValuePtr<> value = build_instruction(my_context, phi_nodes, builder, *jt->expression, value_location);
            if (jt->name) {
              my_context.put(jt->name->text, value);
              function->add_term_name(value, jt->name->text);
            }
          }
        }

        // add values to phi terms
        bt = blocks.begin();
        std::vector<ValuePtr<Phi> >::iterator pt = phi_nodes.begin();
        for (PSI_STD::vector<Parser::Block>::const_iterator it = function_def.blocks.begin();
             it != function_def.blocks.end(); ++it, ++bt) {
          PSI_ASSERT(bt != blocks.end());
          for (PSI_STD::vector<Parser::NamedExpression>::const_iterator jt = it->statements.begin();
               jt != it->statements.end(); ++jt) {
            if (jt->expression->expression_type != Parser::expression_phi)
              continue;

            PSI_ASSERT(pt != phi_nodes.end());
            const Parser::PhiExpression& phi_expr = checked_cast<const Parser::PhiExpression&>(*jt->expression);
            ValuePtr<Phi> phi_term = *pt;
            ++pt;

            for (PSI_STD::vector<Parser::PhiNode>::const_iterator kt = phi_expr.nodes.begin();
                 kt != phi_expr.nodes.end(); ++kt) {
              ValuePtr<Block> block = kt->label ? value_cast<Block>(my_context.get(kt->label->text)) : function->blocks().front();
              ValuePtr<> value = build_expression(my_context, *kt->expression, phi_term->location().logical);
              phi_term->add_edge(block, value);
            }
          }
        }

        PSI_ASSERT(pt == phi_nodes.end());
      }
    }

    AssemblerResult build(Module& module, const PSI_STD::vector<Parser::NamedGlobalElement>& globals) {
      Assembler::AssemblerContext asmct(&module);
      AssemblerResult result;

      for (PSI_STD::vector<Parser::NamedGlobalElement>::const_iterator it = globals.begin();
           it != globals.end(); ++it) {
        LogicalSourceLocationPtr location = module.location().logical->new_child(it->name.text);
        if (it->value->global_type == Parser::global_function) {
          const Parser::Function& def = checked_cast<const Parser::Function&>(*it->value);
          ValuePtr<FunctionType> function_type = Assembler::build_function_type(asmct, def.type, location);
          ValuePtr<Function> function = module.new_function(it->name.text, function_type, SourceLocation(def.location, location));
          function->set_linkage(def.linkage);
          asmct.put(it->name.text, function);
          result[it->name.text] = function;
        } else if (it->value->global_type == Parser::global_variable) {
          const Parser::GlobalVariable& var = checked_cast<const Parser::GlobalVariable&>(*it->value);
          ValuePtr<> global_type = Assembler::build_expression(asmct, *var.type, location);
          ValuePtr<GlobalVariable> global_var = module.new_global_variable(it->name.text, global_type, SourceLocation(var.location, location));
          global_var->set_constant(var.constant);
          global_var->set_linkage(var.linkage);
          asmct.put(it->name.text, global_var);
          result[it->name.text] = global_var;
        } else if (it->value->global_type == Parser::global_recursive) {
          const Parser::RecursiveType& rec = checked_cast<const Parser::RecursiveType&>(*it->value);
          ValuePtr<RecursiveType> recursive_ty = Assembler::build_recursive_type(asmct, rec, location);
          asmct.put(it->name.text, recursive_ty);
          result[it->name.text] = recursive_ty;
        } else {
          const Parser::GlobalDefine& def = checked_cast<const Parser::GlobalDefine&>(*it->value);
          PSI_ASSERT(it->value->global_type == Parser::global_define);
          ValuePtr<> ptr = Assembler::build_expression(asmct, *def.value, location);
          asmct.put(it->name.text, ptr);
        }
      }

      for (PSI_STD::vector<Parser::NamedGlobalElement>::const_iterator it = globals.begin();
           it != globals.end(); ++it) {
        if (it->value->global_type == Parser::global_define)
          continue;

        ValuePtr<> ptr = result[it->name.text];
        PSI_ASSERT(ptr);
        if (ValuePtr<Global> global_ptr = dyn_cast<Global>(ptr)) {
          if (it->value->global_type == Parser::global_function) {
            const Parser::Function& def = checked_cast<const Parser::Function&>(*it->value);
            ValuePtr<Function> function = value_cast<Function>(ptr);
            if (!def.blocks.empty())
              Assembler::build_function(asmct, function, def);
          } else {
            PSI_ASSERT(it->value->global_type == Parser::global_variable);
            const Parser::GlobalVariable& var = checked_cast<const Parser::GlobalVariable&>(*it->value);
            ValuePtr<GlobalVariable> global_var = value_cast<GlobalVariable>(ptr);
            ValuePtr<> value = Assembler::build_expression(asmct, *var.value, global_var->location().logical);
            global_var->set_value(value);
          }
        } else if (ValuePtr<RecursiveType> rec_ptr = dyn_cast<RecursiveType>(ptr)) {
          const Parser::RecursiveType& rec = checked_cast<const Parser::RecursiveType&>(*it->value);
          Assembler::AssemblerContext rct(&asmct);
          RecursiveType::ParameterList::const_iterator ii = rec_ptr->parameters().begin(), ie = rec_ptr->parameters().end();
          PSI_STD::vector<Parser::ParameterExpression>::const_iterator ji = rec.phantom_parameters.begin(), je = rec.phantom_parameters.end();
          for (; ii != ie; ++ii, ++ji) {
            if (ji == je) {
              PSI_ASSERT(je == rec.phantom_parameters.end());
              ji = rec.parameters.begin();
              je = rec.parameters.end();
            }
            rct.put(ji->name->text, *ii);
          }

          rec_ptr->resolve(Assembler::build_expression(rct, *rec.result, rec_ptr->location().logical));
        } else {
          PSI_FAIL("unexpected term type");
        }
      }

      return result;
    }

    AssemblerResult parse_and_build(Module& module, const PhysicalSourceLocation& loc, const char *begin, const char *end) {
      return build(module, parse(module.context().error_context(), SourceLocation(loc, module.location().logical), begin, end));
    }

    AssemblerResult parse_and_build(Module& module, const PhysicalSourceLocation& loc, const char *begin) {
      return parse_and_build(module, loc, begin, begin+std::strlen(begin));
    }
  }
}
