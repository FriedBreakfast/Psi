#include "Compiler.hpp"
#include "Macros.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"

#include <deque>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    class FunctionBodyCompiler {
      TreePtr<EvaluateContext> m_body_context;
      SharedPtr<Parser::TokenExpression> m_body;

    public:
      FunctionBodyCompiler(const TreePtr<EvaluateContext>& body_context,
                           const SharedPtr<Parser::TokenExpression>& body)
      : m_body_context(body_context), m_body(body) {
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        v("body_context", &FunctionBodyCompiler::m_body_context)
        ("body", &FunctionBodyCompiler::m_body);
      }

      TreePtr<Term> evaluate(const TreePtr<Term>& self) {
        PSI_STD::vector<SharedPtr<Parser::NamedExpression> > statements = Parser::parse_statement_list(m_body->text);
        return compile_statement_list(statements, m_body_context, self.location());
      }
    };

    struct FunctionInfo {
      /// \brief C type.
      TreePtr<FunctionType> type;
      /// \brief Name-to-position map.
      PSI_STD::map<String, unsigned> argument_names;

      template<typename V>
      static void visit(V& v) {
        v("type", &FunctionInfo::type)
        ("argument_names", &FunctionInfo::argument_names);
      }
    };
    
    /**
     * \brief Map a parameter mode name to a parameter mode number.
     */
    boost::optional<ParameterMode> parameter_mode_from_name(String name) {
      if (name == "in") return parameter_mode_input;
      else if (name == "out") return parameter_mode_output;
      else if (name == "io") return parameter_mode_io;
      else if (name == "take") return parameter_mode_rvalue;
      else if (name == "const") return parameter_mode_functional;
      else return boost::none;
    }

    /**
     * \brief Map a result mode name to a result mode number.
     */
    boost::optional<ResultMode> result_mode_from_name(String name) {
      if (name == "value") return result_mode_by_value;
      else if (name == "const") return result_mode_functional;
      else if (name == "take") return result_mode_rvalue;
      else if (name == "ref") return result_mode_lvalue;
      else return boost::none;
    }
    
    /**
     * \brief Common function compilation.
     *
     * Figures out the C-level function type and details of how to generate function
     * arguments from what the user writes.
     */
    FunctionInfo compile_function_common(const SharedPtr<Parser::Expression>& implicit_arguments,
                                         const SharedPtr<Parser::Expression>& explicit_arguments,
                                         CompileContext& compile_context,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
      FunctionInfo result;

      SharedPtr<Parser::TokenExpression> implicit_arguments_expr, explicit_arguments_expr;
      if (implicit_arguments && !(implicit_arguments_expr = expression_as_token_type(implicit_arguments, Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "Implicit function arguments not enclosed in (...)");

      if (!(explicit_arguments_expr = expression_as_token_type(explicit_arguments, Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "Explicit function arguments not enclosed in (...)");

      Parser::ImplicitArgumentDeclarations parsed_implicit_arguments;
      if (implicit_arguments_expr)
        parsed_implicit_arguments = Parser::parse_function_argument_implicit_declarations(implicit_arguments_expr->text);
      
      Parser::ArgumentDeclarations parsed_explicit_arguments = Parser::parse_function_argument_declarations(explicit_arguments_expr->text);
      std::map<String, TreePtr<Term> > argument_map;
      PSI_STD::vector<TreePtr<Anonymous> > argument_list;
      PSI_STD::vector<ParameterMode> argument_modes;
      
      // Handle arguments
      for (unsigned n = 0; n != 2; ++n) {
        const PSI_STD::vector<SharedPtr<Parser::FunctionArgument> > *arguments = (n==0) ? &parsed_implicit_arguments.arguments : &parsed_explicit_arguments.arguments;
        for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = arguments->begin(), ie = arguments->end(); ii != ie; ++ii) {
          const Parser::FunctionArgument& argument_expr = **ii;
          PSI_ASSERT(argument_expr.type);

          String expr_name;
          LogicalSourceLocationPtr logical_location;
          if (argument_expr.name) {
            expr_name = String(argument_expr.name->begin, argument_expr.name->end);
            logical_location = location.logical->named_child(expr_name);
          } else {
            logical_location = location.logical->new_anonymous_child();
          }
          SourceLocation argument_location(argument_expr.location.location, logical_location);

          TreePtr<EvaluateContext> argument_context = evaluate_context_dictionary(evaluate_context->module(), argument_location, argument_map, evaluate_context);
          TreePtr<Term> argument_type = compile_expression(argument_expr.type, argument_context, argument_location.logical);
          TreePtr<Anonymous> argument(new Anonymous(argument_type, argument_location));
          argument_list.push_back(argument);
          
          if (argument_expr.mode) {
            String mode_name(argument_expr.mode->begin, argument_expr.mode->end);
            boost::optional<ParameterMode> mode = parameter_mode_from_name(mode_name);
            if (!mode)
              compile_context.error_throw(argument_location, boost::format("Unrecognised argument passing mode: %s") % mode_name);
            argument_modes.push_back(*mode);
          } else {
            argument_modes.push_back((n==0) ? parameter_mode_functional : parameter_mode_io);
          }

          if (argument_expr.name) {
            argument_map[expr_name] = argument;
            result.argument_names[expr_name] = argument_list.size();
          }
        }
      }
      
      PSI_ASSERT(argument_list.size() == argument_modes.size());
      
      // Context used for result type and interfaces
      TreePtr<EvaluateContext> final_argument_context = evaluate_context_dictionary(evaluate_context->module(), location, argument_map, evaluate_context);
      
      // Handle return type
      TreePtr<Term> result_type;
      ResultMode result_mode;
      if (parsed_explicit_arguments.return_type) {
        const Parser::FunctionArgument& argument_expr = *parsed_explicit_arguments.return_type;
        SourceLocation argument_location(argument_expr.location.location, location.logical->new_anonymous_child());
        result_type = compile_expression(argument_expr.type, final_argument_context, location.logical);

        if (argument_expr.mode) {
          String mode_name(argument_expr.mode->begin, argument_expr.mode->end);
          boost::optional<ResultMode> mode = result_mode_from_name(mode_name);
          if (!mode)
            compile_context.error_throw(argument_location, boost::format("Unrecognised result passing mode: %s") % mode_name);
          result_mode = *mode;
        } else {
          result_mode = result_mode_by_value;
        }
      } else {
        result_type = compile_context.builtins().empty_type;
        result_mode = result_mode_functional;
      }
      
      // Handle interfaces
      PSI_STD::vector<TreePtr<InterfaceValue> > interfaces;
      for (PSI_STD::vector<SharedPtr<Parser::Expression> >::const_iterator ii = parsed_implicit_arguments.interfaces.begin(), ie = parsed_implicit_arguments.interfaces.end(); ii != ie; ++ii) {
        TreePtr<Term> interface = compile_expression(*ii, final_argument_context, location.logical);
        TreePtr<InterfaceValue> interface_cast = dyn_treeptr_cast<InterfaceValue>(interface);
        if (!interface_cast) {
          SourceLocation interface_location((*ii)->location.location, location.logical);
          compile_context.error_throw(interface_location, "Interface description did not evaluate to an interface");
        }
        interfaces.push_back(interface_cast);
      }
      
      // Generate function type - parameterize parameters!
      PSI_STD::vector<FunctionParameterType> argument_types;
      for (unsigned ii = 0, ie = argument_list.size(); ii != ie; ++ii)
        argument_types.push_back(FunctionParameterType(argument_modes[ii], argument_list[ii]->parameterize(argument_list[ii].location(), argument_list)));
      TreePtr<Term> result_type_param = result_type->parameterize(result_type.location(), argument_list);
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::iterator ii = interfaces.begin(), ie = interfaces.end(); ii != ie; ++ii)
        *ii = treeptr_cast<InterfaceValue>((*ii)->parameterize(ii->location(), argument_list));
      
      result.type.reset(new FunctionType(result_mode, result_type_param, argument_types, interfaces, location));

      return result;
    }

    /**
     * \brief Compile a function invocation.
     *
     * Argument evaluation order is currently "undefined" (basically determined by the callee,
     * but for now the exact semantics are not going to be guaranteed).
     */
    TreePtr<Term> compile_function_invocation(const FunctionInfo& info, const TreePtr<Term>& function,
                                              const List<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location) {
#if 0
      CompileContext& compile_context = evaluate_context.compile_context();

      if (arguments.size() != 1)
        compile_context.error_throw(location, boost::format("function incovation expects one macro arguments, got %s") % arguments.size());

      SharedPtr<Parser::TokenExpression> parameters;
      if (!(parameters = expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "Parameters argument to function invocation is not a (...)");

      std::map<String, SharedPtr<Parser::Expression> > named_arguments;
      std::deque<SharedPtr<Parser::Expression> > positional_arguments;
      
      PSI_STD::vector<SharedPtr<Parser::NamedExpression> > parsed_arguments = Parser::parse_argument_list(parameters->text);
      for (PSI_STD::vector<SharedPtr<Parser::NamedExpression> >::const_iterator ii = parsed_arguments.begin(), ie = parsed_arguments.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        if (named_expr.name)
          named_arguments[String(named_expr.name->begin, named_expr.name->end)] = named_expr.expression;
        else
          positional_arguments.push_back(named_expr.expression);
      }

      PSI_STD::vector<ArgumentAssignment> previous_arguments;
      PSI_STD::vector<TreePtr<Term> > compiled_arguments;
      for (PSI_STD::vector<ArgumentPassingInfo>::const_iterator ii = info.passing_info.begin(), ie = info.passing_info.end(); ii != ie; ++ii) {
        SharedPtr<Parser::Expression> argument_expr;
        switch (ii->category) {
          case ArgumentPassingInfo::category_positional:
            argument_expr = positional_arguments.front();
            positional_arguments.pop_front();
            break;
            
          case ArgumentPassingInfo::category_keyword: {
            std::map<String, SharedPtr<Parser::Expression> >::iterator ji = named_arguments.find(ii->keyword);
            if (ji != named_arguments.end()) {
              argument_expr = ji->second;
              named_arguments.erase(ji);
            }
            break;
          }
            
          case ArgumentPassingInfo::category_automatic:
            // Default argument never gets a value
            break;
        }

        PSI_STD::vector<TreePtr<Term> > current_arguments;
        if (argument_expr)
          current_arguments = ii->handler->argument_handler(list_from_stl(previous_arguments), *argument_expr);
        else
          current_arguments = ii->handler->argument_default(list_from_stl(previous_arguments));

        if (current_arguments.size() != ii->extra_arguments.size() + 1)
          compile_context.error_throw(location, "User argument processing has produced the wrong number of low level arguments.");

        // Append generated arguments to low level arguments.
        compiled_arguments.insert(compiled_arguments.end(), current_arguments.begin(), current_arguments.end());

        // Append generated arguments to previous argument list.
        for (std::size_t ji = 0, je = ii->extra_arguments.size(); ji != je; ++ji) {
          ArgumentAssignment aa = {ii->extra_arguments[ji].second, current_arguments[ji]};
          previous_arguments.push_back(aa);
        }

        ArgumentAssignment aa_last = {ii->argument, current_arguments.back()};
        previous_arguments.push_back(aa_last);
      }

      // Check all specified arguments have been used
      if (!positional_arguments.empty())
        compile_context.error_throw(location, "Too many positional arguments specified");

      if (!named_arguments.empty()) {
        CompileError err(compile_context, location);
        err.info("Unexpected keyword arguments to function");
        for (std::map<String, SharedPtr<Parser::Expression> >::iterator ii = named_arguments.begin(), ie = named_arguments.end(); ii != ie; ++ii)
          err.info(location.relocate(ii->second->location.location), boost::format("Unexpected keyword: %s") % ii->first);
        err.end();
        throw CompileException();
      }

      return TreePtr<Term>(new FunctionCall(function, compiled_arguments, location));
#endif
    }

    class FunctionInvokeCallback : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      FunctionInvokeCallback(const FunctionInfo& info_, const TreePtr<Term>& function_, const SourceLocation& location)
      : MacroMemberCallback(&vtable, function_.compile_context(), location),
      info(info_),
      function(function_) {
      }
      
      FunctionInfo info;
      TreePtr<Term> function;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<MacroMemberCallback>(v);
        v("info", &FunctionInvokeCallback::info)
        ("function", &FunctionInvokeCallback::function);
      }

      static TreePtr<Term> evaluate_impl(const FunctionInvokeCallback& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        return compile_function_invocation(self.info, self.function, arguments, evaluate_context, location);
      }
    };

    const MacroMemberCallbackVtable FunctionInvokeCallback::vtable =
    PSI_COMPILER_MACRO_MEMBER_CALLBACK(FunctionInvokeCallback, "psi.compiler.FunctionInvokeCallback", MacroMemberCallback);

    /**
     * Create a macro for invoking a function.
     *
     * \param info Argument processing information.
     * \param func Function to call. The arguments of this function must match those
     * expected by \c info.
     */
    TreePtr<Term> function_invoke_macro(const FunctionInfo& info, const TreePtr<Term>& func, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(new FunctionInvokeCallback(info, func, location));
      TreePtr<Macro> macro = make_macro(func.compile_context(), location, callback);
      return make_macro_term(macro, location);
    }

    /**
     * Compile a function definition, and return a macro for invoking it.
     */
    TreePtr<Term> compile_function_definition(const List<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context.compile_context();

      if (arguments.size() != 2)
        compile_context.error_throw(location, boost::format("function macro expects two arguments, got %s") % arguments.size());

      SharedPtr<Parser::TokenExpression> parameters, body;

      if (!(parameters = expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "First (parameters) argument to definition is not a (...)");

      if (!(body = expression_as_token_type(arguments[1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Second (body) parameter to function definition is not a [...]");

#if 0
      FunctionInfo common = compile_function_common(parameters->text, compile_context, evaluate_context, location);

      std::vector<std::pair<ParameterMode, TreePtr<Anonymous> > > argument_trees;
      for (PSI_STD::vector<ArgumentPassingInfo>::iterator ii = common.passing_info.begin(), ie = common.passing_info.end(); ii != ie; ++ii) {
        argument_trees.insert(argument_trees.end(), ii->extra_arguments.begin(), ii->extra_arguments.end());
        argument_trees.push_back(std::make_pair(ii->argument_mode, ii->argument));
      }

      PSI_STD::map<String, TreePtr<Term> > argument_values;
      for (PSI_STD::map<String, unsigned>::iterator ii = common.names.begin(), ie = common.names.end(); ii != ie; ++ii)
        argument_values[ii->first] = common.passing_info[ii->second].argument;

      TreePtr<EvaluateContext> body_context = evaluate_context_dictionary(evaluate_context->module(), location, argument_values, evaluate_context);
      TreePtr<Term> body_tree = tree_callback<Term>(compile_context, location, FunctionBodyCompiler(body_context, body));

      return TreePtr<Function>(new Function(evaluate_context->module(), common.result_mode, common.result_type, argument_trees, body_tree, location));
#endif
    }

    /**
     * \brief Callback to use for constructing interfaces which define functions.
     */
    class FunctionDefineCallback : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      FunctionDefineCallback(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }
      
      static TreePtr<Term> evaluate_impl(const FunctionDefineCallback&,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        return compile_function_definition(arguments, evaluate_context, location);
      }
    };

    const MacroMemberCallbackVtable FunctionDefineCallback::vtable =
    PSI_COMPILER_MACRO_MEMBER_CALLBACK(FunctionDefineCallback, "psi.compiler.FunctionDefineCallback", MacroMemberCallback);

    /**
     * \brief Create a callback to the function definition function.
     */
    TreePtr<Term> function_definition_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(new FunctionDefineCallback(compile_context, location));
      TreePtr<Macro> macro = make_macro(compile_context, location, callback);
      return make_macro_term(macro, location);
    }
  }
}
