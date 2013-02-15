#include "Compiler.hpp"
#include "Macros.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"

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
        return compile_from_bracket(m_body, m_body_context, self.location());
      }
    };

    struct FunctionInfo {
      /// \brief C type.
      TreePtr<FunctionType> type;
      /// \brief Name-to-position map.
      PSI_STD::map<String, unsigned> names;

      template<typename V>
      static void visit(V& v) {
        v("type", &FunctionInfo::type)
        ("names", &FunctionInfo::names);
      }
    };
    
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
      if (implicit_arguments && !(implicit_arguments_expr = expression_as_token_type(implicit_arguments, Parser::token_bracket)))
        compile_context.error_throw(location, "Implicit function arguments not enclosed in (...)");

      if (!(explicit_arguments_expr = expression_as_token_type(explicit_arguments, Parser::token_bracket)))
        compile_context.error_throw(location, "Explicit function arguments not enclosed in (...)");

      Parser::ImplicitArgumentDeclarations parsed_implicit_arguments;
      if (implicit_arguments_expr)
        parsed_implicit_arguments = Parser::parse_function_argument_implicit_declarations(implicit_arguments_expr->text);
      
      Parser::ArgumentDeclarations parsed_explicit_arguments = Parser::parse_function_argument_declarations(explicit_arguments_expr->text);
      std::map<String, TreePtr<Term> > argument_map;
      PSI_STD::vector<TreePtr<Anonymous> > argument_list;
      PSI_STD::vector<ParameterMode> argument_modes;
      
      // Handle implicit arguments
      for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = parsed_implicit_arguments.arguments.begin(), ie = parsed_implicit_arguments.arguments.end(); ii != ie; ++ii) {
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
        argument_modes.push_back(parameter_mode_functional);

        if (argument_expr.name) {
          argument_map[expr_name] = argument;
          result.names[expr_name] = argument_list.size();
        }
      }
      
      // Handle explicit arguments
      for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = parsed_explicit_arguments.arguments.begin(), ie = parsed_explicit_arguments.arguments.end(); ii != ie; ++ii) {
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
        argument_modes.push_back((ParameterMode)argument_expr.mode);

        if (argument_expr.name) {
          argument_map[expr_name] = argument;
          result.names[expr_name] = argument_list.size();
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
        result_mode = (ResultMode)argument_expr.mode;
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
        argument_types.push_back(FunctionParameterType(argument_modes[ii], argument_list[ii]->type->parameterize(argument_list[ii].location(), argument_list)));
      TreePtr<Term> result_type_param = result_type->parameterize(result_type.location(), argument_list);
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::iterator ii = interfaces.begin(), ie = interfaces.end(); ii != ie; ++ii)
        *ii = treeptr_cast<InterfaceValue>((*ii)->parameterize(ii->location(), argument_list));
      
      result.type.reset(new FunctionType(result_mode, result_type_param, argument_types, interfaces, location));

      return result;
    }
    
    namespace {
      struct ArgumentInferrer {
        TreePtr<FunctionType> m_function_type;
        std::vector<TreePtr<Term> > m_explicit_arguments;
        
      public:
        typedef std::vector<TreePtr<Term> > result_type;
        
        ArgumentInferrer(const TreePtr<FunctionType>& function_type, const std::vector<TreePtr<Term> >& explicit_arguments)
        : m_function_type(function_type), m_explicit_arguments(explicit_arguments) {
        }
        
        result_type evaluate(const TreePtr<ValueTree<result_type> >& self) {
          PSI_ASSERT(m_explicit_arguments.size() < m_function_type->parameter_types.size());
          
          unsigned n_implicit = m_function_type->parameter_types.size() - m_explicit_arguments.size();
          PSI_STD::vector<TreePtr<Term> > arguments(n_implicit);
          // Include all arguments so that type dependencies between explicit arguments can be checked
          arguments.insert(arguments.end(), m_explicit_arguments.begin(), m_explicit_arguments.end());
          
          for (unsigned ii = 0, ie = m_explicit_arguments.size(); ii != ie; ++ii) {
            if (!m_explicit_arguments[ii]->type->match(m_function_type->parameter_types[ii].type, arguments, 0))
              self.compile_context().error_throw(m_explicit_arguments[ii].location(), "Incorrect argument type");
          }
          
          // Trim to include only implicit arguments
          arguments.resize(n_implicit);
          for (unsigned ii = 0, ie = n_implicit; ii != ie; ++ii) {
            if (!arguments[ii])
              self.compile_context().error_throw(self.location(), boost::format("No value inferred for argument %d") % (ii+1));
          }
          
          return arguments;
        }
        
        template<typename V>
        static void visit(V& v) {
          v("function_type", &ArgumentInferrer::m_function_type)
          ("explicit_arguments", &ArgumentInferrer::m_explicit_arguments);
        }
      };
      
      struct ArgumentExtractor {
        unsigned m_n;
        TreePtr<ValueTree<ArgumentInferrer::result_type> > m_implicit_arguments;
        
      public:
        typedef Term TreeResultType;
        
        ArgumentExtractor(unsigned n, const TreePtr<ValueTree<ArgumentInferrer::result_type> >& implicit_arguments)
        : m_n(n), m_implicit_arguments(implicit_arguments) {
        }
        
        TreePtr<Term> evaluate(const TreePtr<Term>&) {
          return m_implicit_arguments->value[m_n];
        }
        
        template<typename V>
        static void visit(V& v) {
          v("n", &ArgumentExtractor::m_n)
          ("implicit_arguments", &ArgumentExtractor::m_implicit_arguments);
        }
      };
    }
    
    /**
     * \brief Create a function call.
     * 
     * This will automatically infer explicit arguments.
     * A compilation error is generated if the arguments cannot be inferred,
     * or if the unspecified arguments are not suitable for inferring.
     * 
     * \param explicit_arguments List of explicit arguments.
     */
    TreePtr<Term> function_call(const TreePtr<Term>& function, const PSI_STD::vector<TreePtr<Term> >& explicit_arguments, const SourceLocation& location) {
      CompileContext& compile_context = function.compile_context();

      TreePtr<FunctionType> ftype = dyn_treeptr_cast<FunctionType>(function->type);
      if (!ftype)
        compile_context.error_throw(location, "Call target does not have function type");

      if (explicit_arguments.size() > ftype->parameter_types.size())
        compile_context.error_throw(location, "Too many arguments passed to function");

      unsigned n_implicit = ftype->parameter_types.size() - explicit_arguments.size();
      for (unsigned ii = 0, ie = n_implicit; ii != ie; ++ii) {
        if (ftype->parameter_types[ii].mode != parameter_mode_functional)
          compile_context.error_throw(location, boost::format("Too few arguments passed to function, expected between %d and %d") % (ftype->parameter_types.size() - ii) % ftype->parameter_types.size());
      }

      TreePtr<ValueTree<ArgumentInferrer::result_type> > implicit_args_tree = value_callback(compile_context, location, ArgumentInferrer(ftype, explicit_arguments));
      
      PSI_STD::vector<TreePtr<Term> > all_arguments;
      for (unsigned ii = 0, ie = n_implicit; ii != ie; ++ii)
        all_arguments.push_back(tree_callback(compile_context, location, ArgumentExtractor(ii, implicit_args_tree)));
      all_arguments.insert(all_arguments.end(), explicit_arguments.begin(), explicit_arguments.end());

      return TreePtr<Term>(new FunctionCall(function, all_arguments, location));
    }

    /**
     * \brief Compile a function invocation.
     *
     * Argument evaluation order is currently "undefined" (basically determined by the callee,
     * but for now the exact semantics are not going to be guaranteed).
     */
    TreePtr<Term> compile_function_invocation(const TreePtr<Term>& function,
                                              const List<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context.compile_context();

      if (arguments.size() != 1)
        compile_context.error_throw(location, boost::format("function incovation expects one macro arguments, got %s") % arguments.size());
      
      SharedPtr<Parser::TokenExpression> parameters_expr;
      if (!(parameters_expr = expression_as_token_type(arguments[0], Parser::token_bracket)))
        compile_context.error_throw(location, "Parameters argument to function invocation is not a (...)");

      PSI_STD::vector<SharedPtr<Parser::Expression> > parsed_arguments = Parser::parse_positional_list(parameters_expr->text);
      
      PSI_STD::vector<TreePtr<Term> > explicit_arguments;
      for (PSI_STD::vector<SharedPtr<Parser::Expression> >::const_iterator ii = parsed_arguments.begin(), ie = parsed_arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> value = compile_expression(*ii, evaluate_context, location.logical);
        explicit_arguments.push_back(value);
      }
      
      return function_call(function, explicit_arguments, location);
    }

    class FunctionInvokeCallback : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;

      FunctionInvokeCallback(const TreePtr<Term>& function_, const SourceLocation& location)
      : MacroMemberCallback(&vtable, function_.compile_context(), location),
      function(function_) {
      }
      
      TreePtr<Term> function;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<MacroMemberCallback>(v);
        v("function", &FunctionInvokeCallback::function);
      }

      static TreePtr<Term> evaluate_impl(const FunctionInvokeCallback& self,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        return compile_function_invocation(self.function, arguments, evaluate_context, location);
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
    TreePtr<Term> function_invoke_macro(const TreePtr<Term>& func, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(new FunctionInvokeCallback(func, location));
      TreePtr<Macro> macro = make_macro(func.compile_context(), location, callback);
      return make_macro_term(macro, location);
    }

    /**
     * Compile a function definition, and return a macro for invoking it.
     *
     * \todo Implement return jump target.
     */
    TreePtr<Term> compile_function_definition(const List<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context.compile_context();

      SharedPtr<Parser::Expression> type_arg_1, type_arg_2;
      if (arguments.size() == 2) {
        type_arg_2 = arguments[0];
      } else if (arguments.size() == 3) {
        type_arg_1 = arguments[0];
        type_arg_2 = arguments[1];
      } else {
        compile_context.error_throw(location, boost::format("function macro expects 2 or 3 arguments, got %s") % arguments.size());
      }

      SharedPtr<Parser::TokenExpression> body;
      if (!(body = expression_as_token_type(arguments[arguments.size()-1], Parser::token_square_bracket)))
        compile_context.error_throw(location, "Last (body) parameter to function definition is not a [...]");

      FunctionInfo common = compile_function_common(type_arg_1, type_arg_2, compile_context, evaluate_context, location);

      PSI_STD::vector<TreePtr<Term> > parameter_trees_term; // This exists because C++ won't convert vector<derived> to vector<base>
      PSI_STD::vector<TreePtr<Anonymous> > parameter_trees;
      for (PSI_STD::vector<FunctionParameterType>::const_iterator ii = common.type->parameter_types.begin(), ie = common.type->parameter_types.end(); ii != ie; ++ii) {
        TreePtr<Anonymous> param(new Anonymous(common.type->parameter_type_after(ii->type.location(), parameter_trees_term), ii->type.location()));
        parameter_trees.push_back(param);
        parameter_trees_term.push_back(param);
      }

      PSI_STD::map<String, TreePtr<Term> > argument_values;
      for (PSI_STD::map<String, unsigned>::iterator ii = common.names.begin(), ie = common.names.end(); ii != ie; ++ii)
        argument_values[ii->first] = parameter_trees[ii->second];

      TreePtr<EvaluateContext> body_context = evaluate_context_dictionary(evaluate_context->module(), location, argument_values, evaluate_context);
      TreePtr<Term> body_tree = tree_callback<Term>(compile_context, location, FunctionBodyCompiler(body_context, body));

      return TreePtr<Function>(new Function(evaluate_context->module(), false, common.type, parameter_trees, body_tree, TreePtr<JumpTarget>(), location));
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
      
      template<typename V>
      static void visit(V& v) {
        visit_base<MacroMemberCallback>(v);
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
    
    /**
     * Compile a function definition, and return a macro for invoking it.
     *
     * \todo Implement return jump target.
     */
    TreePtr<Term> compile_function_type(const List<SharedPtr<Parser::Expression> >& arguments,
                                        const TreePtr<EvaluateContext>& evaluate_context,
                                        const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context.compile_context();

      SharedPtr<Parser::Expression> type_arg_1, type_arg_2;
      if (arguments.size() == 1) {
        type_arg_2 = arguments[0];
      } else if (arguments.size() == 2) {
        type_arg_1 = arguments[0];
        type_arg_2 = arguments[1];
      } else {
        compile_context.error_throw(location, boost::format("function_type macro expects 1 or 2 arguments, got %s") % arguments.size());
      }

      return compile_function_common(type_arg_1, type_arg_2, compile_context, evaluate_context, location).type;
    }

    class FunctionTypeCallback : public MacroMemberCallback {
    public:
      static const MacroMemberCallbackVtable vtable;
      
      FunctionTypeCallback(CompileContext& compile_context, const SourceLocation& location)
      : MacroMemberCallback(&vtable, compile_context, location) {
      }

      static TreePtr<Term> evaluate_impl(const FunctionTypeCallback&,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        return compile_function_type(arguments, evaluate_context, location);
      }

      template<typename V>
      static void visit(V& v) {
        visit_base<MacroMemberCallback>(v);
      }
    };
    
    const MacroMemberCallbackVtable FunctionTypeCallback::vtable =
    PSI_COMPILER_MACRO_MEMBER_CALLBACK(FunctionTypeCallback, "psi.compiler.FunctionTypeCallback", MacroMemberCallback);

    TreePtr<Term> function_type_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroMemberCallback> callback(new FunctionTypeCallback(compile_context, location));
      TreePtr<Macro> macro = make_macro(compile_context, location, callback);
      return make_macro_term(macro, location);
    }
  }
}
