#include "Compiler.hpp"
#include "Macros.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"
#include "Function.hpp"

#include <deque>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    const SIVtable ArgumentPassingInfoCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ArgumentPassingInfoCallback", Tree);
    const SIVtable ReturnPassingInfoCallback::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ReturnPassingInfoCallback", Tree);

    class EvaluateContextOneName : public EvaluateContext {
      String m_name;
      TreePtr<Term> m_value;
      TreePtr<EvaluateContext> m_next;

    public:
      static const EvaluateContextVtable vtable;

      EvaluateContextOneName(const SourceLocation& location,
                             const String& name, const TreePtr<Term>& value, const TreePtr<EvaluateContext>& next)
      : EvaluateContext(&vtable, next->module(), location),
      m_name(name), m_value(value), m_next(next) {
        m_vptr = reinterpret_cast<const SIVtable*>(&vtable);
      }

      template<typename Visitor>
      static void visit(Visitor& v) {
        visit_base<EvaluateContext>(v);
        v("name", &EvaluateContextOneName::m_name)
        ("value", &EvaluateContextOneName::m_value)
        ("next", &EvaluateContextOneName::m_next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(const EvaluateContextOneName& self, const String& name, const SourceLocation& location, const TreePtr<EvaluateContext>& evaluate_context) {
        if (name == self.m_name) {
          return lookup_result_match(self.m_value);
        } else if (self.m_next) {
          return self.m_next->lookup(name, location, evaluate_context);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable EvaluateContextOneName::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextOneName, "psi.compiler.EvaluateContextOneName", EvaluateContext);

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

    /**
     * \brief An argument passed by matching patterns against the types of other arguments.
     */
    struct PatternArgument {
      /// \brief Whether any data is actually passed for this argument.
      PsiBool ghost;
      TreePtr<Anonymous> value;
    };

    /**
     * \brief Arguments which are located using the interface mechanism, and appear as
     * available interfaces inside the function.
     */
    struct InterfaceArgument {
      TreePtr<Term> type;
    };

    /**
     * \brief Used to pass previous argument information to later arguments in case they use it for processing.
     */
    struct ArgumentAssignment {
      /// \brief The term which represented this value during argument construction.
      TreePtr<Anonymous> argument;
      /**
       * \brief The replacement value.
       *
       * Note that due to the generic type system, the type of this value may not
       * be the same as the type of \c argument.
       */
      TreePtr<Term> value;
    };

    struct ArgumentHandlerVtable {
      TreeVtable base;
      void (*argument_default) (PSI_STD::vector<TreePtr<Term> >*, const ArgumentHandler*, const void*, void*);
      void (*argument_handler) (PSI_STD::vector<TreePtr<Term> >*, const ArgumentHandler*, const void*, void*, const Parser::Expression*);
    };

    /**
     * \brief Argument handler term interface.
     */
    class ArgumentHandler : public Tree {
    public:
      typedef ArgumentHandlerVtable VtableType;
      static const SIVtable vtable;
      
      PSI_STD::vector<TreePtr<Term> > argument_default(const List<ArgumentAssignment>& previous) const {
        ResultStorage<PSI_STD::vector<TreePtr<Term> > > result;
        derived_vptr(this)->argument_default(result.ptr(), this, previous.vptr(), previous.object());
        return result.done();
      }

      PSI_STD::vector<TreePtr<Term> > argument_handler(const List<ArgumentAssignment>& previous, const Parser::Expression& expr) const {
        ResultStorage<PSI_STD::vector<TreePtr<Term> > > result;
        derived_vptr(this)->argument_handler(result.ptr(), this, previous.vptr(), previous.object(), &expr);
        return result.done();
      }
    };

    const SIVtable ArgumentHandler::vtable = PSI_COMPILER_TREE_ABSTRACT("psi.compiler.ArgumentHandler", Tree);

    template<typename Derived>
    struct ArgumentHandlerWrapper : NonConstructible {
      static void argument_default(ArgumentHandler *self) {
        Derived::argument_default_impl(*static_cast<Derived*>(self));
      }

      static void argument_handler(ArgumentHandler *self) {
        Derived::argument_handler_impl(*static_cast<Derived*>(self));
      }
    };

#define PSI_COMPILER_ARGUMENT_HANDLER(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &ArgumentHandlerWrapper<derived>::argument_default, \
    &ArgumentHandlerWrapper<derived>::argument_handler \
  }

    struct FunctionInfo {
      /// \brief C type.
      TreePtr<FunctionType> type;
      /// \brief Result mode.
      ResultMode result_mode;
      /// \brief Result type, using Anonymous arguments in \c passing_info for values.
      TreePtr<Term> result_type;
      /// \brief How to translate user-specified arguments to C-type arguments
      PSI_STD::vector<ArgumentPassingInfo> passing_info;
      /// \brief Argument names to position.
      PSI_STD::map<String, unsigned> names;

      template<typename V>
      static void visit(V& v) {
        v("type", &FunctionInfo::type)
        ("result_type", &FunctionInfo::result_type)
        ("passing_info", &FunctionInfo::passing_info)
        ("names", &FunctionInfo::names);
      }
    };

    /**
     * \brief Common function compilation.
     *
     * Figures out the C-level function type and details of how to generate function
     * arguments from what the user writes.
     */
    FunctionInfo compile_function_common(const Parser::ParserLocation& arguments,
                                         CompileContext& compile_context,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
      Parser::ArgumentDeclarations parsed_arguments = Parser::parse_function_argument_declarations(arguments);

      FunctionInfo result;
      std::vector<std::pair<ParameterMode, TreePtr<Anonymous> > > type_arguments;

      TreePtr<EvaluateContext> argument_context = evaluate_context;
      for (std::vector<SharedPtr<Parser::NamedExpression> >::const_iterator ii = parsed_arguments.arguments.begin(), ie = parsed_arguments.arguments.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        PSI_ASSERT(named_expr.expression);

        String expr_name;
        LogicalSourceLocationPtr logical_location;
        if (named_expr.name) {
          expr_name = String(named_expr.name->begin, named_expr.name->end);
          logical_location = location.logical->named_child(expr_name);
        } else {
          logical_location = location.logical->new_anonymous_child();
        }
        SourceLocation argument_location(named_expr.location.location, logical_location);

        TreePtr<Term> argument_expr = compile_expression(named_expr.expression, argument_context, argument_location.logical);
        TreePtr<ArgumentPassingInfoCallback> passing_info_callback = interface_lookup_as<ArgumentPassingInfoCallback>(compile_context.builtins().argument_passing_info_interface, argument_expr, location);

        ArgumentPassingInfo passing_info = passing_info_callback->argument_passing_info();

        type_arguments.insert(type_arguments.end(), passing_info.extra_arguments.begin(), passing_info.extra_arguments.end());
        type_arguments.push_back(std::make_pair(passing_info.argument_mode, passing_info.argument));

        if (named_expr.name) {
          result.names[expr_name] = result.passing_info.size();
          argument_context.reset(new EvaluateContextOneName(argument_location, expr_name, passing_info.argument, argument_context));
        }

        result.passing_info.push_back(passing_info);
      }

      if (parsed_arguments.return_type) {
        TreePtr<Term> result_expr = compile_expression(parsed_arguments.return_type, argument_context, location.logical);
        if (!result.result_type->is_type())
          compile_context.error_throw(location, "Function result type expression does not evaluate to a type");
        TreePtr<ReturnPassingInfoCallback> return_info_callback = interface_lookup_as<ReturnPassingInfoCallback>(compile_context.builtins().return_passing_info_interface, result_expr, location);
        ReturnPassingInfo return_info = return_info_callback->return_passing_info();
        result.result_type = return_info.type;
        result.result_mode = return_info.mode;
      } else {
        result.result_type = compile_context.builtins().empty_type;
        result.result_mode = result_mode_functional;
      }

      result.type.reset(new FunctionType(result.result_mode, result.result_type, type_arguments, location));

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
    }

    class FunctionInvokeCallback : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;

      FunctionInvokeCallback(const FunctionInfo& info_, const TreePtr<Term>& function_, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, function_.compile_context(), location),
      info(info_),
      function(function_) {
      }
      
      FunctionInfo info;
      TreePtr<Term> function;
      
      template<typename V>
      static void visit(V& v) {
        visit_base<MacroEvaluateCallback>(v);
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

    const MacroEvaluateCallbackVtable FunctionInvokeCallback::vtable =
    PSI_COMPILER_MACRO_EVALUATE_CALLBACK(FunctionInvokeCallback, "psi.compiler.FunctionInvokeCallback", MacroEvaluateCallback);

    /**
     * Create a macro for invoking a function.
     *
     * \param info Argument processing information.
     * \param func Function to call. The arguments of this function must match those
     * expected by \c info.
     */
    TreePtr<Term> function_invoke_macro(const FunctionInfo& info, const TreePtr<Term>& func, const SourceLocation& location) {
      TreePtr<MacroEvaluateCallback> callback(new FunctionInvokeCallback(info, func, location));
      TreePtr<Macro> macro = make_macro(func.compile_context(), location, callback);
      return make_macro_term(func.compile_context(), location, macro);
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

      TreePtr<Function> func(new Function(evaluate_context->module(), common.result_mode, common.result_type, argument_trees, body_tree, location));

      // Create macro to interpret function arguments
      return function_invoke_macro(common, func, location);
    }

    /**
     * \brief Callback to use for constructing interfaces which define functions.
     */
    class FunctionDefineCallback : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;
      
      FunctionDefineCallback(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(&vtable, compile_context, location) {
      }
      
      static TreePtr<Term> evaluate_impl(const FunctionDefineCallback&,
                                         const TreePtr<Term>&,
                                         const List<SharedPtr<Parser::Expression> >& arguments,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
        return compile_function_definition(arguments, evaluate_context, location);
      }
    };

    const MacroEvaluateCallbackVtable FunctionDefineCallback::vtable =
    PSI_COMPILER_MACRO_EVALUATE_CALLBACK(FunctionDefineCallback, "psi.compiler.FunctionDefineCallback", MacroEvaluateCallback);

    /**
     * \brief Create a callback to the function definition function.
     */
    TreePtr<Term> function_definition_macro(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroEvaluateCallback> callback(new FunctionDefineCallback(compile_context, location));
      TreePtr<Macro> macro = make_macro(compile_context, location, callback);
      return make_macro_term(compile_context, location, macro);
    }
  }
}
