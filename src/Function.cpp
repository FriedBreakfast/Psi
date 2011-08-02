#include "Compiler.hpp"
#include "Parser.hpp"
#include "Tree.hpp"
#include "Utility.hpp"

#include <boost/bind.hpp>
#include <boost/format.hpp>

namespace Psi {
  namespace Compiler {
    SharedPtr<Parser::TokenExpression> expression_as_token_type(const SharedPtr<Parser::Expression>& expr, Parser::TokenExpression::TokenType type) {
      if (expr->expression_type != Parser::expression_token)
        return SharedPtr<Parser::TokenExpression>();

      SharedPtr<Parser::TokenExpression> cast_expr = checked_pointer_cast<Parser::TokenExpression>(expr);
      if (cast_expr->token_type != type)
        return SharedPtr<Parser::TokenExpression>();

      return cast_expr;
    }

    class EvaluateContextOneName : public EvaluateContext {
      String m_name;
      TreePtr<Term> m_value;
      TreePtr<EvaluateContext> m_next;

    public:
      static const EvaluateContextVtable vtable;

      EvaluateContextOneName(CompileContext& compile_context, const SourceLocation& location,
                             const String& name, const TreePtr<Term>& value, const TreePtr<EvaluateContext>& next)
      : EvaluateContext(compile_context, location),
      m_name(name), m_value(value), m_next(next) {
        m_vptr = reinterpret_cast<const SIVtable*>(&vtable);
      }

      template<typename Visitor>
      static void visit_impl(EvaluateContextOneName& self, Visitor& visitor) {
        PSI_FAIL("not implemented");
        visitor
        ("name", self.m_name)
        ("value", self.m_value)
        ("next", self.m_next);
      }

      static LookupResult<TreePtr<Term> > lookup_impl(EvaluateContextOneName& self, const String& name) {
        if (name == self.m_name) {
          return lookup_result_match(self.m_value);
        } else if (self.m_next) {
          return self.m_next->lookup(name);
        } else {
          return lookup_result_none;
        }
      }
    };

    const EvaluateContextVtable EvaluateContextOneName::vtable =
    PSI_COMPILER_EVALUATE_CONTEXT(EvaluateContextOneName, "psi.compiler.EvaluateContextOneName", EvaluateContext);

    class FunctionBodyCompiler : public Dependency {
      TreePtr<EvaluateContext> m_body_context;
      SharedPtr<Parser::TokenExpression> m_body;

    public:
      static const DependencyVtable vtable;

      FunctionBodyCompiler(const TreePtr<EvaluateContext>& body_context,
                           const SharedPtr<Parser::TokenExpression>& body)
      : m_body_context(body_context), m_body(body) {
        PSI_COMPILER_DEPENDENCY_INIT();
      }

      template<typename Visitor>
      static void visit_impl(FunctionBodyCompiler& self, Visitor& visitor) {
        PSI_FAIL("not implemented");
        visitor
        ("body_context", self.m_body_context)
        ("body", self.m_body);
      }

      static void run_impl(FunctionBodyCompiler& self, const TreePtr<Function>& function) {
        std::vector<SharedPtr<Parser::NamedExpression> > statements = Parser::parse_statement_list(self.m_body->text);
        TreePtr<Term> body_tree = compile_statement_list(list_from_stl(statements), self.m_body_context, function->location());
        function->body = body_tree;
        body_tree->complete(true);
      }
    };

    const DependencyVtable FunctionBodyCompiler::vtable = PSI_COMPILER_DEPENDENCY(FunctionBodyCompiler, "psi.compiler.FunctionBodyCompiler", Function);

    class ArgumentHandler;

    /**
     * \brief An argument passed by matching patterns against the types of other arguments.
     */
    struct PatternArgument {
      /// \brief Whether any data is actually passed for this argument.
      PsiBool ghost;
      TreePtr<FunctionTypeArgument> value;
    };

    /**
     * \brief Arguments which are located using the interface mechanism, and appear as
     * available interfaces inside the function.
     */
    struct InterfaceArgument {
      TreePtr<Term> type;
    };

    struct ArgumentPassingInfo {
      enum Category {
        category_positional,
        category_keyword,
        category_automatic
      };

      char category;
      String keyword;

      PSI_STD::vector<PatternArgument> pattern_arguments;      
      PSI_STD::vector<InterfaceArgument> interface_arguments;
      /// \brief Type of this function argument.
      TreePtr<Term> type;
      /// \brief Handler used to interpret the argument.
      TreePtr<ArgumentHandler> handler;
    };

    class ArgumentPassingInfoCallback;
    
    struct ArgumentPassingInfoCallbackVtable {
      void (*argument_passing_info) (ArgumentPassingInfo*, ArgumentPassingInfoCallback*);
    };

    class ArgumentPassingInfoCallback : public Tree {
      const ArgumentPassingInfoCallbackVtable* derived_vptr() {return reinterpret_cast<const ArgumentPassingInfoCallbackVtable*>(m_vptr);}

    public:
      ArgumentPassingInfo argument_passing_info() {
        ResultStorage<ArgumentPassingInfo> result;
        derived_vptr()->argument_passing_info(result.ptr(), this);
        return result.done();
      }
    };

    struct ArgumentHandlerVtable {
      TreeVtable base;
      void (*argument_default) (ArgumentHandler*);
      void (*argument_handler) (ArgumentHandler*);
    };

    class ArgumentHandler : public Tree {
      const ArgumentHandlerVtable *derived_vptr() {return reinterpret_cast<const ArgumentHandlerVtable*>(m_vptr);}
      
    public:
      void argument_default() {
        return derived_vptr()->argument_default(this);
      }

      void argument_handler() {
        return derived_vptr()->argument_handler(this);
      }
    };

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

    struct FunctionArgumentInfo {
      /// \brief Argument handler
      TreePtr<ArgumentHandler> handler;
      /// \brief C function argument this corresponds to.
      int index;
    };

    struct FunctionInfo {
      /// \brief C type.
      TreePtr<FunctionType> type;
      /// \brief Handlers for setting up positional arguments at the call site.
      PSI_STD::vector<FunctionArgumentInfo> positional_arguments;
      /// \brief Handlers for setting up keyword arguments at the call site.
      PSI_STD::map<String, FunctionArgumentInfo> keyword_arguments;
      /// \brief Arguments which are passed by default (i.e. are entirely dependent on previous arguments and other context).
      PSI_STD::vector<FunctionArgumentInfo> automatic_arguments;

      PSI_STD::map<String, unsigned> argument_names;
    };

    FunctionInfo compile_function_common(const Parser::ParserLocation& arguments,
                                         CompileContext& compile_context,
                                         const TreePtr<EvaluateContext>& evaluate_context,
                                         const SourceLocation& location) {
      Parser::ArgumentDeclarations parsed_arguments = Parser::parse_function_argument_declarations(arguments);

      FunctionInfo result;
      result.type.reset(new FunctionType(compile_context, location));

      TreePtr<EvaluateContext> argument_context = evaluate_context;
      for (std::vector<SharedPtr<Parser::NamedExpression> >::const_iterator ii = parsed_arguments.arguments.begin(), ib = parsed_arguments.arguments.begin(), ie = parsed_arguments.arguments.end(); ii != ie; ++ii) {
        const Parser::NamedExpression& named_expr = **ii;
        PSI_ASSERT(named_expr.expression);

        String expr_name = named_expr.name ? String(named_expr.name->begin, named_expr.name->end) : String();
        SourceLocation argument_location(named_expr.location.location, make_logical_location(location.logical, expr_name));

        TreePtr<Term> argument_expr = compile_expression(named_expr.expression, argument_context, argument_location.logical);
        TreePtr<ArgumentPassingInfoCallback> passing_info_callback = interface_lookup_as<ArgumentPassingInfoCallback>(compile_context.argument_passing_info_interface(), argument_expr, location);

        ArgumentPassingInfo passing_info = passing_info_callback->argument_passing_info();

        for (PSI_STD::vector<PatternArgument>::iterator ii = passing_info.pattern_arguments.begin(), ie = passing_info.pattern_arguments.end(); ii != ie; ++ii) {
          result.type->arguments.push_back(ii->value);
          ii->value->set_parent(result.type);
        }

        for (PSI_STD::vector<InterfaceArgument>::iterator ii = passing_info.interface_arguments.begin(), ie = passing_info.interface_arguments.end(); ii != ie; ++ii) {
          TreePtr<FunctionTypeArgument> arg(new FunctionTypeArgument(ii->type, argument_location));
          result.type->arguments.push_back(arg);
          arg->set_parent(result.type);
        }
        
        TreePtr<FunctionTypeArgument> argument(new FunctionTypeArgument(passing_info.type, argument_location));
        argument->set_parent(result.type);
        result.type->arguments.push_back(argument);

        FunctionArgumentInfo argument_info;
        argument_info.index = result.type->arguments.size() - 1;
        argument_info.handler = passing_info.handler;

        switch (passing_info.category) {
        case ArgumentPassingInfo::category_positional:
          result.positional_arguments.push_back(argument_info);
          break;
          
        case ArgumentPassingInfo::category_keyword: {
          String keyword;
          if (!passing_info.keyword.empty())
            keyword = passing_info.keyword;
          else if (named_expr.name)
            keyword = expr_name;
          else
            compile_context.error_throw(argument_location, "No name given for keyword argument");
          result.keyword_arguments[keyword] = argument_info;
          break;
        }
          
        case ArgumentPassingInfo::category_automatic:
          result.automatic_arguments.push_back(argument_info);
          break;

        default:
          compile_context.error_throw(argument_location, "Invalid argument passing category", CompileContext::error_internal);
        }
        
        if (named_expr.name) {
          argument_context.reset(new EvaluateContextOneName(compile_context, argument_location, expr_name, argument, argument_context));
          result.argument_names[expr_name] = argument_info.index;
        }
      }

      if (parsed_arguments.return_type) {
        TreePtr<> result_type = compile_expression(parsed_arguments.return_type, argument_context, location.logical);
        TreePtr<Type> cast_result_type = dyn_treeptr_cast<Type>(result_type);
        if (!cast_result_type)
          compile_context.error_throw(location, "Function result type expression does not evaluate to a type");
        result.type->result_type = cast_result_type;
      } else {
        result.type->result_type = compile_context.empty_type();
      }

      return result;
    }

    TreePtr<Term> compile_function_definition(const List<SharedPtr<Parser::Expression> >& arguments,
                                              const TreePtr<EvaluateContext>& evaluate_context,
                                              const SourceLocation& location) {
      CompileContext& compile_context = evaluate_context->compile_context();

      if (arguments.size() != 2)
        compile_context.error_throw(location, boost::format("function macro expects two arguments, got %s") % arguments.size());

      SharedPtr<Parser::TokenExpression> parameters, body;

      if (!(parameters = expression_as_token_type(arguments[0], Parser::TokenExpression::bracket)))
        compile_context.error_throw(location, "First (parameters) argument to definition is not a (...)");

      if (!(body = expression_as_token_type(arguments[1], Parser::TokenExpression::square_bracket)))
        compile_context.error_throw(location, "Second (body) parameter to function definition is not a [...]");

      FunctionInfo common = compile_function_common(parameters->text, compile_context, evaluate_context, location);

      PSI_STD::map<TreePtr<Term>, TreePtr<Term> > argument_substitutions;
      PSI_STD::vector<TreePtr<FunctionArgument> > argument_trees;

      for (PSI_STD::vector<TreePtr<FunctionTypeArgument> >::iterator ii = common.type->arguments.begin(), ie = common.type->arguments.end(); ii != ie; ++ii) {
        TreePtr<Term> arg_type = (*ii)->type()->rewrite((*ii)->location(), Map<TreePtr<Term>, TreePtr<Term> >(argument_substitutions));
        TreePtr<FunctionArgument> arg(new FunctionArgument(arg_type, (*ii)->location()));
        argument_substitutions[*ii] = arg;
        argument_trees.push_back(arg);
      }
      
      TreePtr<> result_type = common.type->result_type->rewrite(location, argument_substitutions);
      TreePtr<Type> cast_result_type = dyn_treeptr_cast<Type>(result_type);
      if (!cast_result_type)
        compile_context.error_throw(location, "Rewritten function result type is not a type");

      PSI_STD::map<String, TreePtr<Term> > argument_values;
      for (PSI_STD::map<String, unsigned>::iterator ii = common.argument_names.begin(), ie = common.argument_names.end(); ii != ie; ++ii)
        argument_values[ii->first] = argument_trees[ii->second];

      TreePtr<EvaluateContext> body_context = evaluate_context_dictionary(compile_context, location, argument_values, evaluate_context);
      DependencyPtr body_compiler(new FunctionBodyCompiler(body_context, body));
      TreePtr<Function> function(new Function(common.type, location, body_compiler));
      function->result_type = cast_result_type;
      function->arguments.swap(argument_trees);

      return function;
    }

    /**
     * \brief Callback to use for constructing interfaces which define functions.
     */
    class FunctionDefineCallback : public MacroEvaluateCallback {
    public:
      static const MacroEvaluateCallbackVtable vtable;
      
      FunctionDefineCallback(CompileContext& compile_context, const SourceLocation& location)
      : MacroEvaluateCallback(compile_context, location) {
        PSI_COMPILER_TREE_INIT();
      }
      
      static TreePtr<Term> evaluate_impl(FunctionDefineCallback&,
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
    TreePtr<Term> function_definition_object(CompileContext& compile_context, const SourceLocation& location) {
      TreePtr<MacroEvaluateCallback> callback(new FunctionDefineCallback(compile_context, location));
      TreePtr<Macro> macro = make_macro(compile_context, location, "function", callback);
      
      TreePtr<Term> term = make_macro_term(compile_context, location);
      attach_compile_implementation(compile_context.macro_interface(), treeptr_cast<ImplementationTerm>(term->type()), macro, location);
      
      return term;
    }
  }
}
