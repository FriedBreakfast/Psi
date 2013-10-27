#include "Compiler.hpp"
#include "Macros.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"
#include "Interface.hpp"
#include "Implementation.hpp"
#include "TermBuilder.hpp"

#include <boost/format.hpp>
#include <boost/next_prior.hpp>

namespace Psi {
  namespace Compiler {
    /**
     * \brief Convert a ParameterMode to a ResultMode
     */
    TermMode parameter_to_term_mode(ParameterMode mode) {
      switch (mode) {
      case parameter_mode_input:
      case parameter_mode_output:
      case parameter_mode_io:
      case parameter_mode_rvalue: return term_mode_lref;
      case parameter_mode_functional:
      case parameter_mode_phantom: return term_mode_value;
      default: PSI_FAIL("unknown enum value");
      }
    }

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

      TreePtr<Term> evaluate(const TreePtr<Function>& self) {
        return compile_from_bracket(m_body, m_body_context, self->location());
      }
    };
    
    /**
     * \brief Function argument information, from which function types can be produced.
     */
    struct FunctionArgumentInfo {
      PSI_STD::map<String, unsigned> argument_names;
      PSI_STD::vector<TreePtr<Anonymous> > arguments;
      PSI_STD::vector<ParameterMode> argument_modes;
      PSI_STD::vector<TreePtr<InterfaceValue> > interfaces;
      TreePtr<Term> result_type;
      ResultMode result_mode;
    };

    /**
     * Compile function argument specification.
     */
    FunctionArgumentInfo compile_function_arguments(const SharedPtr<Parser::Expression>& function_arguments,
                                                    CompileContext& compile_context,
                                                    const TreePtr<EvaluateContext>& evaluate_context,
                                                    const SourceLocation& location) {
      SharedPtr<Parser::TokenExpression> function_arguments_expr;
      if (!(function_arguments_expr = expression_as_token_type(function_arguments, Parser::token_bracket)))
        compile_context.error_throw(location, "Function arguments not enclosed in (...)");
      
      Parser::FunctionArgumentDeclarations parsed_arguments = Parser::parse_function_argument_declarations(compile_context.error_context(), location.logical, function_arguments_expr->text);
      FunctionArgumentInfo result;
      std::map<String, TreePtr<Term> > argument_map;
      
      // Handle implicit arguments for which=0, explicit arguments for which=1
      for (unsigned which = 0; which < 2; ++which) {
        bool is_implicit = (which == 0);
        const PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >& decl = is_implicit ? parsed_arguments.implicit : parsed_arguments.arguments;
        
        for (PSI_STD::vector<SharedPtr<Parser::FunctionArgument> >::const_iterator ii = decl.begin(), ie = decl.end(); ii != ie; ++ii) {
          const Parser::FunctionArgument& argument_expr = **ii;
          PSI_ASSERT(argument_expr.type);

          String expr_name;
          LogicalSourceLocationPtr logical_location;
          if (argument_expr.name) {
            expr_name = String(argument_expr.name->begin, argument_expr.name->end);
            logical_location = location.logical->new_child(expr_name);
          } else {
            logical_location = location.logical;
          }
          SourceLocation argument_location(argument_expr.location, logical_location);
          TreePtr<EvaluateContext> argument_context = evaluate_context_dictionary(evaluate_context->module(), argument_location, argument_map, evaluate_context);
          
          if (!argument_expr.is_interface) {
            // A parameter
            TreePtr<Term> argument_type = compile_term(argument_expr.type, argument_context, argument_location.logical);
            TermMode argument_mode = is_implicit ? term_mode_value : parameter_to_term_mode(argument_expr.mode);
            TreePtr<Anonymous> argument = TermBuilder::anonymous(argument_type, argument_mode, argument_location);

            if (argument_expr.name) {
              argument_map[expr_name] = argument;
              result.argument_names[expr_name] = result.arguments.size();
            }

            result.arguments.push_back(argument);
            result.argument_modes.push_back(is_implicit ? parameter_mode_functional : argument_expr.mode);
          } else {
            // An interface specification
            result.interfaces.push_back(compile_interface_value(argument_expr.type, argument_context, location.logical));
          }
        }
      }
      
      PSI_ASSERT(result.arguments.size() == result.argument_modes.size());

      // Handle return type
      TreePtr<EvaluateContext> result_context = evaluate_context_dictionary(evaluate_context->module(), location, argument_map, evaluate_context);
      if (parsed_arguments.return_type) {
        result.result_type = compile_term(parsed_arguments.return_type, result_context, location.logical);
        result.result_mode = parsed_arguments.return_mode;
      } else {
        result.result_type = compile_context.builtins().empty_type;
        result.result_mode = result_mode_by_value;
      }

      return result;
    }

    struct FunctionInfo {
      /// \brief C type.
      TreePtr<FunctionType> type;
      /// \brief Name-to-position map.
      PSI_STD::map<String, unsigned> names;
    };
    
    /**
     * Convert a FunctionArgumentInfo to a function type.
     */
    TreePtr<FunctionType> function_arguments_to_type(const FunctionArgumentInfo& arg_info, const SourceLocation& location) {
      // Generate function type - parameterize parameters!
      PSI_STD::vector<FunctionParameterType> argument_types;
      for (unsigned ii = 0, ie = arg_info.arguments.size(); ii != ie; ++ii)
        argument_types.push_back(FunctionParameterType(arg_info.argument_modes[ii], arg_info.arguments[ii]->type->parameterize(arg_info.arguments[ii]->location(), arg_info.arguments)));
      TreePtr<Term> result_type = arg_info.result_type->parameterize(arg_info.result_type->location(), arg_info.arguments);
      
      PSI_STD::vector<TreePtr<InterfaceValue> > interfaces;
      for (PSI_STD::vector<TreePtr<InterfaceValue> >::const_iterator ii = arg_info.interfaces.begin(), ie = arg_info.interfaces.end(); ii != ie; ++ii)
        interfaces.push_back(treeptr_cast<InterfaceValue>((*ii)->parameterize((*ii)->location(), arg_info.arguments)));
      
      return TermBuilder::function_type(arg_info.result_mode, result_type, argument_types, interfaces, location);
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
      CompileContext& compile_context = function->compile_context();

      TreePtr<FunctionType> ftype = term_unwrap_dyn_cast<FunctionType>(function->type);
      if (!ftype)
        compile_context.error_throw(location, "Call target does not have function type");

      if (explicit_arguments.size() > ftype->parameter_types.size())
        compile_context.error_throw(location, "Too many arguments passed to function");

      unsigned n_implicit = ftype->parameter_types.size() - explicit_arguments.size();
      for (unsigned ii = 0, ie = n_implicit; ii != ie; ++ii) {
        if (ftype->parameter_types[ii].mode != parameter_mode_functional)
          compile_context.error_throw(location, boost::format("Too few arguments passed to function, expected between %d and %d") % (ftype->parameter_types.size() - ii) % ftype->parameter_types.size());
      }

      PSI_STD::vector<TreePtr<Term> > all_arguments(n_implicit);
      // Include all arguments so that type dependencies between explicit arguments can be checked
      all_arguments.insert(all_arguments.end(), explicit_arguments.begin(), explicit_arguments.end());
      
      for (unsigned ii = 0, ie = explicit_arguments.size(); ii != ie; ++ii) {
        if (!ftype->parameter_types[ii].type->match(explicit_arguments[ii]->type, Term::upref_match_read, all_arguments))
          compile_context.error_throw(location, boost::format("Incorrect argument type at position %d") % (ii+1));
      }

      return TermBuilder::function_call(function, all_arguments, location);
    }
    
    /**
     * \brief Parse arguments for a macro which has the syntax of a function call.
     */
    PSI_STD::vector<TreePtr<Term> > compile_call_arguments(const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                                           const TreePtr<EvaluateContext>& evaluate_context,
                                                           const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context->compile_context();

      if (arguments.size() != 1)
        compile_context.error_throw(location, boost::format("call incovation expects one macro argument, got %s") % arguments.size());
      
      SharedPtr<Parser::TokenExpression> parameters_expr;
      if (!(parameters_expr = Parser::expression_as_token_type(arguments[0], Parser::token_bracket)))
        compile_context.error_throw(location, "Parameters argument to call is not a (...)");

      PSI_STD::vector<SharedPtr<Parser::Expression> > parsed_arguments = Parser::parse_positional_list(compile_context.error_context(), location.logical, parameters_expr->text);
      
      PSI_STD::vector<TreePtr<Term> > result;
      for (PSI_STD::vector<SharedPtr<Parser::Expression> >::const_iterator ii = parsed_arguments.begin(), ie = parsed_arguments.end(); ii != ie; ++ii)
        result.push_back(compile_term(*ii, evaluate_context, location.logical));
      
      return result;
    }

    /**
     * \brief Compile a function invocation.
     *
     * Argument evaluation order is currently "undefined" (basically determined by the callee,
     * but for now the exact semantics are not going to be guaranteed).
     */
    TreePtr<Term> compile_function_invocation(const TreePtr<Term>& function,
                                              const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location) {
      PSI_STD::vector<TreePtr<Term> > explicit_arguments = compile_call_arguments(arguments, evaluate_context, location);
      return function_call(function, explicit_arguments, location);
    }

    class FunctionInvokeCallback : public Macro {
    public:
      static const MacroVtable vtable;

      FunctionInvokeCallback(const TreePtr<Term>& function_, const SourceLocation& location)
      : Macro(&vtable, function_->compile_context(), location),
      function(function_) {
      }
      
      TreePtr<Term> function;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Macro>(v);
        v("function", &FunctionInvokeCallback::function);
      }

      static TreePtr<Term> evaluate_impl(const FunctionInvokeCallback& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const MacroTermArgument&,
                                         const SourceLocation& location) {
        return compile_function_invocation(self.function, arguments, evaluate_context, location);
      }
    };

    const MacroVtable FunctionInvokeCallback::vtable = PSI_COMPILER_MACRO(FunctionInvokeCallback, "psi.compiler.FunctionInvokeCallback", Macro, TreePtr<Term>, MacroTermArgument);

    /**
     * Create a macro for invoking a function.
     *
     * \param info Argument processing information.
     * \param func Function to call. The arguments of this function must match those
     * expected by \c info.
     */
    TreePtr<Term> function_invoke_macro(const TreePtr<Term>& func, const SourceLocation& location) {
      TreePtr<Macro> macro(::new FunctionInvokeCallback(func, location));
      return make_macro_term(macro, location);
    }

    /**
     * \brief Function macro.
     */
    class FunctionMacro : public Macro {
    public:
      static const MacroVtable vtable;
      
      FunctionMacro(CompileContext& compile_context, const SourceLocation& location)
      : Macro(&vtable, compile_context, location) {
      }
      
      static TreePtr<Term> evaluate_impl(const FunctionMacro& self,
                                         const TreePtr<Term>&,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const MacroTermArgument&,
                                         const SourceLocation& location) {
        switch (arguments.size()) {
        case 1: {
          FunctionArgumentInfo arg_info = compile_function_arguments(arguments[0], self.compile_context(), evaluate_context, location);
          return function_arguments_to_type(arg_info, location);
        }
          
        case 2: {
          CompileContext& compile_context = evaluate_context->compile_context();

          SharedPtr<Parser::Expression> type_arg = arguments[0];

          FunctionArgumentInfo arg_info = compile_function_arguments(arguments[0], self.compile_context(), evaluate_context, location);

          SharedPtr<Parser::TokenExpression> body;
          if (!(body = Parser::expression_as_token_type(arguments[1], Parser::token_square_bracket)))
            compile_context.error_throw(location, "Body parameter to function definition is not a [...]");
          
          TreePtr<FunctionType> type = function_arguments_to_type(arg_info, location);

          PSI_STD::map<String, TreePtr<Term> > argument_values;
          for (PSI_STD::map<String, unsigned>::const_iterator ii = arg_info.argument_names.begin(), ie = arg_info.argument_names.end(); ii != ie; ++ii) {
            PSI_ASSERT(ii->second < arg_info.arguments.size());
            argument_values[ii->first] = arg_info.arguments[ii->second];
          }

          TreePtr<EvaluateContext> body_context = evaluate_context_dictionary(evaluate_context->module(), location, argument_values, evaluate_context);

          /// \todo Implement function linkage specification.
          return TermBuilder::function(evaluate_context->module(), type, link_private, arg_info.arguments, TreePtr<JumpTarget>(), location,
                                      FunctionBodyCompiler(body_context, body));
        }
          
        default:
          self.compile_context().error_throw(location, "function macro expects one or two arguments");
        }
      }
      
      template<typename V>
      static void visit(V& v) {
        visit_base<Macro>(v);
      }
    };

    const MacroVtable FunctionMacro::vtable = PSI_COMPILER_MACRO(FunctionMacro, "psi.compiler.FunctionMacro", Macro, TreePtr<Term>, MacroTermArgument);
    
    class FunctionInterfaceMemberCallback : public InterfaceMemberCallback {
    public:
      static const VtableType vtable;
      
      FunctionInterfaceMemberCallback(CompileContext& compile_context, const SourceLocation& location)
      : InterfaceMemberCallback(&vtable, compile_context, location) {}

      static TreePtr<Term> evaluate_impl(const FunctionInterfaceMemberCallback& self,
                                         const TreePtr<Interface>& interface,
                                         const PSI_STD::vector<unsigned>& path,
                                         const PSI_STD::vector<SharedPtr<Parser::Expression> >& parameters_expr,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        TreePtr<Term> pattern = interface_member_pattern(interface, path, location);
        
        TreePtr<FunctionType> function_pattern;
        if (TreePtr<PointerType> ptr_pattern = term_unwrap_dyn_cast<PointerType>(pattern))
          function_pattern = term_unwrap_dyn_cast<FunctionType>(ptr_pattern->target_type);
        
        if (!function_pattern)
          self.compile_context().error_throw(location, "Interface member is not a pointer-to-function as is expected", CompileError::error_internal);
        
        PSI_STD::vector<TreePtr<Term> > parameters = compile_call_arguments(parameters_expr, evaluate_context, location);
        if (parameters.size() != function_pattern->parameter_types.size() - 1)
          self.compile_context().error_throw(location, boost::format("Wrong number of parameters to function call, expected %d, got %d") % (function_pattern->parameter_types.size()-1) % parameters.size());
        
        PSI_STD::vector<TreePtr<Term> > function_wildcards;
        PSI_STD::vector<TreePtr<Term> > interface_wildcards(interface->pattern.size());
        for (std::size_t ii = 0, ie = parameters.size(); ii != ie; ++ii) {
          if (!function_pattern->parameter_types[ii+1].type->match2(parameters[ii]->type, Term::upref_match_read, function_wildcards, interface_wildcards))
            self.compile_context().error_throw(location, "Function parameters do not match interface pattern");
        }
        
        if (std::count(interface_wildcards.begin(), interface_wildcards.end(), TreePtr<Term>()) > 0)
          self.compile_context().error_throw(location, "Parameters passed to interface function matched required pattern, but did not give values for all interface parameters");
        
        TreePtr<Term> interface_val = TermBuilder::interface_value(interface, interface_wildcards, location);
        TreePtr<Term> member_ptr = TermBuilder::exists_value(interface_val, location);
        for (PSI_STD::vector<unsigned>::const_iterator ii = path.begin(), ie = path.end(); ii != ie; ++ii)
          member_ptr = TermBuilder::element_pointer(member_ptr, *ii, location);
        
        TreePtr<Term> function_ref = TermBuilder::ptr_target(TermBuilder::ptr_target(member_ptr, location), location);
        parameters.insert(parameters.begin(), interface_val);
        
        return TermBuilder::function_call(function_ref, parameters, location);
      }
      
      static TreePtr<Term> implement_impl(const FunctionInterfaceMemberCallback& self,
                                          const ImplementationMemberSetup& setup,
                                          const SharedPtr<Parser::Expression>& value,
                                          const TreePtr<EvaluateContext>& evaluate_context,
                                          const SourceLocation& location) {
        SharedPtr<Parser::TokenExpression> params_expr, body_expr;
        SharedPtr<Parser::EvaluateExpression> cast_expr = Parser::expression_as_evaluate(value);
        if (cast_expr && (cast_expr->parameters.size() == 1)) {
          params_expr = Parser::expression_as_token_type(cast_expr->object, Parser::token_bracket);
          body_expr = Parser::expression_as_token_type(cast_expr->parameters[0], Parser::token_square_bracket);
        }
        
        if (!params_expr || !body_expr)
          self.compile_context().error_throw(location, "Implementation of interface function was not of the form '(...) [...]'");

        PSI_STD::vector<Parser::TokenExpression> parameter_name_exprs = Parser::parse_identifier_list(self.compile_context().error_context(), location.logical, params_expr->text);
        PSI_STD::vector<SourceLocation> parameter_locations;
        for (PSI_STD::vector<Parser::TokenExpression>::const_iterator ii = parameter_name_exprs.begin(), ie = parameter_name_exprs.end(); ii != ie; ++ii)
          parameter_locations.push_back(SourceLocation(ii->location, location.logical->new_child(ii->text.str())));
        
        TreePtr<FunctionType> func_type;
        if (TreePtr<PointerType> ptr_type = term_unwrap_dyn_cast<PointerType>(setup.type))
          func_type = term_unwrap_dyn_cast<FunctionType>(ptr_type->target_type);
        
        if (!func_type)
          self.compile_context().error_throw(location, "Implementation member was not a function as expected", CompileError::error_internal);
        
        ImplementationFunctionSetup fn_setup = implementation_function_setup(func_type, location, parameter_locations);
        
        if (parameter_name_exprs.size() > fn_setup.parameters.size())
          self.compile_context().error_throw(location, boost::format("Too many parameter names in function implementation") % fn_setup.parameters.size() % parameter_name_exprs.size());
        
        PSI_STD::map<String, TreePtr<Term> > parameter_names;
        for (std::size_t ii = 0, ie = parameter_name_exprs.size(); ii != ie; ++ii)
          parameter_names.insert(std::make_pair(parameter_name_exprs[ii].text.str(), fn_setup.parameters[fn_setup.parameters.size() - (ie - ii)]));
        
        TreePtr<EvaluateContext> body_context = evaluate_context_dictionary(evaluate_context->module(), location, parameter_names, evaluate_context);
        TreePtr<Term> body = compile_from_bracket(body_expr, evaluate_context, location);
        
        return implementation_function_finish(setup.base, fn_setup, evaluate_context->module(), body);
      }
    };
    
    const InterfaceMemberCallbackVtable FunctionInterfaceMemberCallback::vtable = PSI_COMPILER_INTERFACE_MEMBER_CALLBACK(FunctionInterfaceMemberCallback, "psi.compiler.FunctionInterfaceMemberCallback", InterfaceMemberCallback);
    
    class FunctionInterfaceMemberMacro : public Macro {
    public:
      static const MacroVtable vtable;
      
      FunctionInterfaceMemberMacro(CompileContext& compile_context, const SourceLocation& location)
      : Macro(&vtable, compile_context, location) {}
      template<typename V> static void visit(V& v) {visit_base<Macro>(v);}
      
      static InterfaceMemberResult evaluate_impl(const FunctionInterfaceMemberMacro& self,
                                                 const TreePtr<Term>& PSI_UNUSED(value),
                                                 const PSI_STD::vector<SharedPtr<Parser::Expression> >& arguments,
                                                 const TreePtr<EvaluateContext>& evaluate_context,
                                                 const InterfaceMemberArgument& argument,
                                                 const SourceLocation& location) {
        if (arguments.size() != 1)
          self.compile_context().error_throw(location, boost::format("function macro in interface definition expects 1 argument, got %s") % arguments.size());

        SharedPtr<Parser::Expression> type_arg = arguments[0];

        FunctionArgumentInfo info = compile_function_arguments(type_arg, self.compile_context(), evaluate_context, location);
        // Note that the indices in info.argument_names might need to
        // be incremented since an argument has been inserted at the front,
        // but they aren't actually used here
        info.arguments.insert(info.arguments.begin(), TermBuilder::anonymous(argument.self_pointer_type, term_mode_value, location));
        info.argument_modes.insert(info.argument_modes.begin(), parameter_mode_functional);
        
        InterfaceMemberResult result;
        result.type = TermBuilder::pointer(function_arguments_to_type(info, location), location);
        result.callback.reset(::new FunctionInterfaceMemberCallback(self.compile_context(), location));
        
        return result;
      }
    };
    
    const MacroVtable FunctionInterfaceMemberMacro::vtable = PSI_COMPILER_MACRO(FunctionInterfaceMemberMacro, "psi.compiler.FunctionInterfaceMemberMacro", Macro, InterfaceMemberResult, InterfaceMemberArgument);

    /**
     * \brief Create a callback to the function definition function.
     */
    TreePtr<Term> function_macro(CompileContext& compile_context, const SourceLocation& location) {
      PSI_STD::vector<ConstantMetadataSetup> md;
      
      ConstantMetadataSetup term_eval;
      term_eval.type = compile_context.builtins().type_macro;
      term_eval.value.reset(::new FunctionMacro(compile_context, location));
      term_eval.n_wildcards = 0;
      term_eval.pattern.push_back(compile_context.builtins().macro_term_tag);
      md.push_back(term_eval);
      
      ConstantMetadataSetup interface_eval;
      interface_eval.type = compile_context.builtins().type_macro;
      interface_eval.value.reset(::new FunctionInterfaceMemberMacro(compile_context, location));
      interface_eval.n_wildcards = 0;
      interface_eval.pattern.push_back(compile_context.builtins().macro_interface_member_tag);
      md.push_back(interface_eval);
      
      return make_annotated_type(compile_context, md, location);
    }
  }
}
